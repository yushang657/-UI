// ============================================================
// Charlie UI · 壳层实现
// 开屏 / 闭屏动画 + 页面导航 + 免责弹窗 + 品牌区
//
// 【重建说明】
//   原文件在一次数组拼接编辑中文件头被覆盖, 丢失了 #include、全局状态、
//   DrawSidebar / DrawTitleBar 定义体、以及所有公开接口的实现。
//   本段为重写版本, 与后半段保留的 Draw 主体拼接。
//   设计上保持原有约束: 圆角不裁控件、岛常驻、明暗遮罩只覆盖面板。
// ============================================================
#include "CharlieShell.h"
#include "CharlieTheme.h"
#include "CharlieState.h"
#include "CharliePages.h"
#include "CharlieWidgets.h"
#include "CharlieIsland.h"
#include "CharlieFreeFps.h"
#include "CharlieAnim.h"

#include "../platform/CharlieTouch.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace charlie {
namespace shell {

// 控件库全部在 charlie::ui 下 (Combo / Toast / Button / BtnKind /
// themeTransition / DrawToasts ...)。本文件是壳层, 通篇都在拼装这些控件,
// 逐个写 ui:: 前缀只会让布局代码更难读。
// 只引入 ui, 不引 charlie 整体: island / freefps / touch 仍保留显式前缀,
// 避免与 shell 自己的 Toggle() 之类同名函数冲突。
using namespace charlie::ui;

// SafeDt / ExpApproach / ease::* / Timeline / Anim 由 CharlieAnim 提供

// ============================================================
// 全局状态
// ============================================================
namespace {

WinState g_state         = WinState::Hidden;
float    g_openProgress  = 0.f;
float    g_closeProgress = 0.f;

Timeline g_openTL;
Timeline g_closeTL;

float  g_contentAlpha    = 0.f;
float  g_pageFade        = 1.f;
int    g_currentPage     = PAGE_HOME;
bool   g_pageResetScroll = false;

bool   g_dragging = false;
ImVec2 g_dragStartMouse(0.f, 0.f);
ImVec2 g_dragStartOffset(0.f, 0.f);
ImVec2 g_dragOffset(0.f, 0.f);

bool   g_disclaimerAgreed = false;
bool   g_disclaimerDrag   = false;
ImVec2 g_disclaimerOffset(0.f, 0.f);

bool   g_exitRequested = false;
bool   g_quitArmed     = false;

char   g_disclaimerPath[512] = "./charlie_agreed.flag";

LayoutCfg g_cfg;

// 指针边沿检测的上一帧状态。放在文件作用域而不是函数内,
// 因为多个控件需要共享同一份"本帧是否刚按下"的判定;
// 若各自持有 static, 兄弟控件的更新顺序会让部分控件永远读不到边沿。
bool g_prevDown = false;

} // namespace

LayoutCfg& Cfg() { return g_cfg; }

// ============================================================
// 免责声明持久化
// ============================================================
namespace {

void LoadDisclaimerFlag() {
    FILE* fp = fopen(g_disclaimerPath, "rb");
    if (!fp) return;
    char line[128] = {0};
    const size_t n = fread(line, 1, sizeof(line) - 1, fp);
    fclose(fp);
    if (n == 0) return;
    line[n] = 0;
    g_disclaimerAgreed = (strstr(line, "agree") != nullptr);
}

void SaveDisclaimerFlag() {
    FILE* fp = fopen(g_disclaimerPath, "wb");
    if (!fp) return;
    fputs("agree=1\n", fp);
    fclose(fp);
}

} // namespace

bool DisclaimerAgreed() { return g_disclaimerAgreed; }

void SetDisclaimerPath(const char* path) {
    if (path && path[0])
        snprintf(g_disclaimerPath, sizeof(g_disclaimerPath), "%s", path);
    else
        snprintf(g_disclaimerPath, sizeof(g_disclaimerPath), "%s",
                 "./charlie_agreed.flag");
    LoadDisclaimerFlag();
}

// ============================================================
// 配置持久化
// ============================================================
void SaveConfig() {
    FILE* fp = fopen("./charlie_layout.cfg", "wb");
    if (!fp) return;
    fprintf(fp, "uiScale %.4f\n",    g_cfg.uiScale);
    fprintf(fp, "themeIdx %d\n",     g_cfg.themeIdx);
    fprintf(fp, "islandStyle %d\n",  g_cfg.islandStyle);
    fprintf(fp, "islandTop %.4f\n",  g_cfg.islandTop);
    fprintf(fp, "navSide %d\n",      g_cfg.navSide);
    fprintf(fp, "fontId %d\n",       g_cfg.fontId);
    fprintf(fp, "dragX %.3f\n",      g_dragOffset.x);
    fprintf(fp, "dragY %.3f\n",      g_dragOffset.y);
    fprintf(fp, "page %d\n",         g_currentPage);
    fprintf(fp, "dlgX %.3f\n",       g_disclaimerOffset.x);
    fprintf(fp, "dlgY %.3f\n",       g_disclaimerOffset.y);
    fclose(fp);
}

void LoadConfig() {
    FILE* fp = fopen("./charlie_layout.cfg", "rb");
    if (!fp) return;
    char key[64];
    while (fscanf(fp, "%63s", key) == 1) {
        if      (!strcmp(key, "uiScale"))     fscanf(fp, "%f", &g_cfg.uiScale);
        else if (!strcmp(key, "themeIdx"))    fscanf(fp, "%d", &g_cfg.themeIdx);
        else if (!strcmp(key, "islandStyle")) fscanf(fp, "%d", &g_cfg.islandStyle);
        else if (!strcmp(key, "islandTop"))   fscanf(fp, "%f", &g_cfg.islandTop);
        else if (!strcmp(key, "navSide"))     fscanf(fp, "%d", &g_cfg.navSide);
        else if (!strcmp(key, "fontId"))      fscanf(fp, "%d", &g_cfg.fontId);
        else if (!strcmp(key, "dragX"))       fscanf(fp, "%f", &g_dragOffset.x);
        else if (!strcmp(key, "dragY"))       fscanf(fp, "%f", &g_dragOffset.y);
        else if (!strcmp(key, "page"))        fscanf(fp, "%d", &g_currentPage);
        else if (!strcmp(key, "dlgX"))        fscanf(fp, "%f", &g_disclaimerOffset.x);
        else if (!strcmp(key, "dlgY"))        fscanf(fp, "%f", &g_disclaimerOffset.y);
        else {
            // 未知键: 吞到行尾, 避免把值当成新键再读一遍
            int ch;
            while ((ch = fgetc(fp)) != EOF && ch != '\n') {}
        }
    }
    fclose(fp);
}

// ============================================================
// 状态与控制
// ============================================================
WinState State() { return g_state; }
bool IsVisible() { return g_state != WinState::Hidden; }

void Init(float screenW, float screenH, float userScale) {
    charlie::RebuildMetrics(screenW, screenH, userScale);

    g_state         = WinState::Hidden;
    g_openProgress  = 0.f;
    g_closeProgress = 0.f;
    g_contentAlpha  = 0.f;
    g_pageFade      = 1.f;
    g_dragging      = false;
    g_dragOffset    = ImVec2(0.f, 0.f);
    g_exitRequested = false;
    g_quitArmed     = false;
    g_prevDown      = false;

    g_openTL.Reset();
    g_closeTL.Reset();

    LoadConfig();
    LoadDisclaimerFlag();

    // 配置里的用户缩放优先于传入值
    if (g_cfg.uiScale > 0.01f) {
        charlie::RebuildMetrics(screenW, screenH, g_cfg.uiScale);
    }
    charlie::ApplyStyle();

    printf("[Charlie] shell::Init %.0fx%.0f scale=%.2f navSide=%d fontId=%d\n",
           screenW, screenH, userScale, g_cfg.navSide, g_cfg.fontId);
}

void RequestOpen() {
    if (g_state == WinState::Opening || g_state == WinState::Opened) return;
    g_state         = WinState::Opening;
    g_closeProgress = 0.f;
    g_openProgress  = 0.f;
    g_openTL.Start(ImGui::GetTime(), 0.30f);
    island::ResetAnimation();
}

void RequestClose() {
    if (g_state == WinState::Closing || g_state == WinState::Hidden) return;
    g_state         = WinState::Closing;
    g_closeProgress = 0.f;
    g_closeTL.Start(ImGui::GetTime(), 0.22f);
    CloseOpenCombo();
}

void Toggle() {
    if (g_state == WinState::Hidden || g_state == WinState::Closing)
        RequestOpen();
    else if (g_state == WinState::Opened)
        RequestClose();
    // Opening / Closing 中间态不处理, 天然防连点抖动
}

void ForceClose() {
    g_state         = WinState::Hidden;
    g_openProgress  = 0.f;
    g_closeProgress = 0.f;
    g_contentAlpha  = 0.f;
    g_dragging      = false;
    g_openTL.Reset();
    g_closeTL.Reset();
    CloseOpenCombo();
}

void RequestExit() { g_exitRequested = true; }
bool ExitRequested() { return g_exitRequested; }

bool QuitArmed() { return g_quitArmed; }
void ResetQuitArm() { g_quitArmed = false; }

void OnRenderStackRebuilt() {
    // popup 栈随旧 ImGui 上下文销毁, 免责弹窗必须在新上下文里重新 Open,
    // 否则再也弹不出来。用户已确认的状态不重置。
    g_disclaimerDrag = false;
    g_prevDown       = false;
    charlie::AnimResetAll();
    freefps::Reset();
    island::ResetAnimation();
}

// ============================================================
// 结束进程按钮 (两段式确认)
// ============================================================
bool QuitButton(const char* id, float width, float height) {
    const Metrics& m = M();
    const Palette& p = P();

    ImGui::PushID(id);
    const ImVec2 cp = ImGui::GetCursorScreenPos();

    const float w = (width  > 0.f) ? width  : ImGui::GetContentRegionAvail().x;
    const float h = (height > 0.f) ? height : m.rowH;

    ImGui::InvisibleButton("##qb", ImVec2(w, h));

    // 指针 (触摸优先)
    float tx = 0.f, ty = 0.f;
    bool  tdown = false;
    if (charlie::touch::IsRunning()) {
        charlie::touch::GetPos(&tx, &ty);
        tdown = charlie::touch::IsDown();
    } else {
        const ImGuiIO& io = ImGui::GetIO();
        tx = io.MousePos.x; ty = io.MousePos.y; tdown = io.MouseDown[0];
    }
    const bool hov = (tx >= cp.x && tx <= cp.x + w &&
                      ty >= cp.y && ty <= cp.y + h);
    const bool edge = tdown && !g_prevDown;

    bool done = false;
    if (edge && hov) {
        if (!g_quitArmed) {
            g_quitArmed = true;          // 第一次点击: 进入确认态
            Toast("再点一次结束进程", true);
        } else {
            g_quitArmed = false;
            RequestExit();
            done = true;
        }
    }

    // 视觉
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0 = cp;
    const ImVec2 b1(cp.x + w, cp.y + h);

    const ImU32 bgc = g_quitArmed
        ? WithAlpha(p.danger, hov ? 0.34f : 0.24f)
        : (hov ? p.bgItemHover : p.bgItem);
    dl->AddRectFilled(b0, b1, bgc, m.radiusS);
    dl->AddRect(b0, b1,
                WithAlpha(g_quitArmed ? p.danger : p.border,
                          g_quitArmed ? 0.85f : 0.55f),
                m.radiusS, 0, ImMax(1.f, m.s * 0.9f));

    const char* label = g_quitArmed ? "确认结束" : "结束进程";
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(b0.x + (w - ts.x) * 0.5f, b0.y + (h - ts.y) * 0.5f),
                g_quitArmed ? p.danger : p.textMid, label);

    ImGui::PopID();
    return done;
}

// ============================================================
// 免责弹窗
// ============================================================
namespace {

void DrawDisclaimer(const ImVec2& disp) {
    if (g_disclaimerAgreed) return;

    const Metrics& m = M();
    const Palette& p = P();

    const float w = ImMin(disp.x * 0.82f, 620.f * m.s);
    const float h = ImMin(disp.y * 0.62f, 400.f * m.s);
    ImVec2 c(disp.x * 0.5f + g_disclaimerOffset.x,
             disp.y * 0.5f + g_disclaimerOffset.y);
    ImVec2 wp(c.x - w * 0.5f, c.y - h * 0.5f);
    ImVec2 ws(w, h);

    // 遮罩: 只压暗, 不夺走整屏的视觉
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), disp, IM_COL32(0, 0, 0, 128));

    ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ws, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m.radiusL);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToVec4(p.bgWin));

    if (ImGui::Begin("##charlie_disclaimer", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), p.bgWin, m.radiusL);
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                    WithAlpha(p.border, 0.8f), m.radiusL, 0, 1.2f);

        // 顶部强调条
        dl->AddRectFilled(ImVec2(wp.x + m.padX, wp.y + m.padY),
                          ImVec2(wp.x + m.padX + 3.f * m.s,
                                 wp.y + m.padY + m.fontL * 1.5f),
                          p.accent, 1.5f * m.s);

        ImGui::SetCursorScreenPos(ImVec2(wp.x + m.padX * 2.f,
                                         wp.y + m.padY));
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
        ImGui::TextUnformatted("使用前须知");
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(wp.x + m.padX,
                                         wp.y + m.padY + m.fontL * 1.9f));
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
        ImGui::PushTextWrapPos(wp.x + ws.x - m.padX);
        ImGui::TextUnformatted(
            "本程序仅用于界面演示与交互验证。\n\n"
            "所有开关、滑块与数值均为占位控件，"
            "不包含任何游戏逻辑、驱动调用或数据读写。\n\n"
            "继续使用即表示你已知晓上述说明。");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        // 按钮
        const float bh = m.rowH * 1.05f;
        const float bw = ImMin(240.f * m.s, ws.x - m.padX * 2.f);
        ImGui::SetCursorScreenPos(
            ImVec2(wp.x + (ws.x - bw) * 0.5f, wp.y + ws.y - m.padY - bh));
        if (Button("我已知晓", BtnKind::Primary, bw)) {
            g_disclaimerAgreed = true;
            SaveDisclaimerFlag();
            charlie::ui::Toast("已确认");
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace

// ============================================================
// 侧栏
// ============================================================
namespace {

void DrawSidebar(const ImVec2& pos, const ImVec2& size, bool onRight) {
    const Metrics& m = M();
    const Palette& p = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      WithAlpha(p.bgSide, 0.92f), m.radius);

    // 与内容区之间的分隔线
    const float lx = onRight ? pos.x : (pos.x + size.x);
    dl->AddLine(ImVec2(lx, pos.y + m.gap), ImVec2(lx, pos.y + size.y - m.gap),
                WithAlpha(p.divider, 0.55f), ImMax(1.f, m.s * 0.9f));

    // ---------- 品牌头 ----------
    // 高度与 titleH 解耦: 标题栏加高不该把导航按钮挤出可视区。
    const float bh = ImMin(size.y * 0.16f, 52.f * m.s) + m.gapS * 1.4f;
    {
        const float cx = pos.x + size.x * 0.5f;
        const float cy = pos.y + bh * 0.5f;

        const float dotR = 4.2f * m.s;
        dl->AddCircleFilled(ImVec2(cx, cy - 10.f * m.s), dotR,
                            WithAlpha(p.accent, 0.95f), 18);
        dl->AddCircle(ImVec2(cx, cy - 10.f * m.s), dotR * 1.9f,
                      WithAlpha(p.accent, 0.22f), 20, ImMax(1.f, m.s * 0.8f));

        const ImVec2 bs = ImGui::CalcTextSize("Charlie");
        dl->AddText(ImVec2(cx - bs.x * 0.5f, cy + 2.f * m.s),
                    WithAlpha(p.textHi, 0.96f), "Charlie");
    }

    // ---------- 导航按钮 ----------
    const float navTop = pos.y + bh + m.padY * 1.4f;
    const float btnH   = m.rowH * 0.94f;
    const float inset  = m.gapS * 1.2f;

    // 指针 (触摸优先) 与按下边沿。边沿用文件级 g_prevDown,
    // 保证整个侧栏只会有一个按钮在"刚按下"这一帧命中。
    float tx = 0.f, ty = 0.f;
    bool  tdown = false;
    if (charlie::touch::IsRunning()) {
        charlie::touch::GetPos(&tx, &ty);
        tdown = charlie::touch::IsDown();
    } else {
        const ImGuiIO& io = ImGui::GetIO();
        tx = io.MousePos.x; ty = io.MousePos.y; tdown = io.MouseDown[0];
    }
    const bool edge = tdown && !g_prevDown;

    float y = navTop;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        const float by1 = y + btnH;
        if (by1 > pos.y + size.y - m.padY - m.rowH * 1.6f) break;

        const ImVec2 b0(pos.x + inset, y);
        const ImVec2 b1(pos.x + size.x - inset, by1);

        const bool active = (g_currentPage == i);
        const bool hover  = (tx >= b0.x && tx <= b1.x &&
                             ty >= b0.y && ty <= b1.y);

        if (active) {
            dl->AddRectFilled(b0, b1, WithAlpha(p.accent, 0.16f), m.radiusS);
            const float bx = onRight ? (b1.x - 2.4f * m.s) : b0.x;
            dl->AddRectFilled(ImVec2(bx, b0.y + btnH * 0.22f),
                              ImVec2(bx + 2.4f * m.s, b1.y - btnH * 0.22f),
                              p.accent, 1.3f * m.s);
        } else if (hover) {
            dl->AddRectFilled(b0, b1, p.bgItemHover, m.radiusS);
        }

        const char* name = PageName(i);
        const ImVec2 ns = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(b0.x + (b1.x - b0.x - ns.x) * 0.5f,
                           b0.y + (btnH - ns.y) * 0.5f),
                    active ? p.accent : p.textMid, name);

        if (edge && hover && g_currentPage != i) {
            g_currentPage     = i;
            g_pageResetScroll = true;
            g_pageFade        = 0.f;
            CloseOpenCombo();
            charlie::AnimResetAll();
        }

        y = by1 + m.gapS * 0.9f;
    }

    // ---------- 底部退出按钮 ----------
    {
        const float qh = btnH;
        const float qy = pos.y + size.y - m.padY - qh;
        if (qy > y + m.gapS) {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + inset, qy));
            ImGui::PushItemWidth(size.x - inset * 2.f);
            QuitButton("##quitSide", size.x - inset * 2.f, qh);
            ImGui::PopItemWidth();
        }
    }
}

// ============================================================
// 标题栏
// ============================================================
void DrawTitleBar(const ImVec2& pos, const ImVec2& size) {
    const Metrics& m = M();
    const Palette& p = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---------- 标题 + 描述 ----------
    {
        const float tx = pos.x + m.padX;
        const float ty = pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;

        const char* title = PageName(g_currentPage);
        dl->AddText(ImVec2(tx, ty), p.textHi, title);

        const ImVec2 tu = ImGui::CalcTextSize(title);
        const char* desc = PageDesc(g_currentPage);
        if (desc && desc[0]) {
            dl->AddText(ImVec2(tx + tu.x + m.gapS * 1.6f, ty + 1.f * m.s),
                        WithAlpha(p.textLow, 0.85f), desc);
        }
    }

    // ---------- 右侧明暗切换按钮 ----------
    const float rx = pos.x + size.x - m.padX - m.radiusL * 0.55f;

    const float btnD = 44.f * m.s;
    const float hotP = 8.f * m.s;

    // 【垂直留白约束】
    //   窗口圆角 radiusL 会吃掉标题栏上沿的角。按钮顶若落进圆角弧内,
    //   顶部就被削掉一块(历史缺陷, 已实测确认)。
    //   titleH = 84*s 使留白达 20*s > radiusL(18*s)。
    //   sink 是额外的视觉下沉量, 让按钮重心略低于标题文字.
    const float sink    = 3.f * m.s;
    const float usableV = size.y;
    ImVec2 b1(rx, pos.y + (usableV - btnD) * 0.5f + sink);
    ImVec2 b0(b1.x - btnD, b1.y);

    const ImVec2 h0(b0.x - hotP, b0.y - hotP);
    const ImVec2 h1(b1.x + hotP, b1.y + hotP);
    ImGui::SetCursorScreenPos(h0);
    ImGui::InvisibleButton("##themeBtn",
                           ImVec2(btnD + hotP * 2.f, btnD + hotP * 2.f));

    // 指针 (触摸优先)
    float tx = 0.f, ty = 0.f;
    bool  tdown = false;
    if (charlie::touch::IsRunning()) {
        charlie::touch::GetPos(&tx, &ty);
        tdown = charlie::touch::IsDown();
    } else {
        const ImGuiIO& io = ImGui::GetIO();
        tx = io.MousePos.x; ty = io.MousePos.y; tdown = io.MouseDown[0];
    }
    const bool hov  = (tx >= h0.x && tx <= h1.x && ty >= h0.y && ty <= h1.y);
    const bool edge = tdown && !g_prevDown;

    const bool  dark  = IsDark();
    const float ha    = hov ? 1.f : 0.f;
    const float darkA = charlie::Anim(&g_cfg.themeIdx, dark, 14.f);

    const ImVec2 cc((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
    const float  hd = btnD * 0.5f;

    dl->AddCircleFilled(cc, hd, Mix(p.bgItem, p.bgItemHover, ha), 32);
    dl->AddCircle(cc, hd, WithAlpha(p.border, 0.55f * (0.4f + 0.6f * ha)),
                  hd, ImMax(1.f, m.s * 0.9f));

    const float rad = btnD * 0.235f;
    const ImU32 ic  = Mix(p.textMid, p.accent, ha);

    // ---------- 太阳 ----------
    if (darkA < 0.995f) {
        const float t   = 1.f - darkA;
        const float rot = darkA * 1.5707963f;
        const float sr  = rad * (0.60f + 0.40f * t);
        const ImU32 sc  = WithAlpha(ic, t);

        ImVec2 prev(cc.x + sr * cosf(rot), cc.y + sr * sinf(rot));
        for (int i = 1; i <= 24; ++i) {
            const float ang = (float)i * (6.2831853f / 24.f);
            const ImVec2 pt(cc.x + sr * cosf(ang + rot),
                            cc.y + sr * sinf(ang + rot));
            dl->AddLine(prev, pt, sc, 2.f * m.s);
            prev = pt;
        }

        // 光芒端点 0.390*btnD, 距底板边缘(0.5*btnD)留约 11% 呼吸空间。
        // 原 2.00(=0.470*btnD) 几乎顶到描边, 视觉上像被切掉一角。
        const float r0 = sr * 1.34f;
        const float r1 = sr * 1.66f;
        for (int i = 0; i < 8; ++i) {
            const float ang = (float)i * 0.7853982f + rot;
            const float ax = cosf(ang), ay = sinf(ang);
            dl->AddLine(ImVec2(cc.x + ax * r0, cc.y + ay * r0),
                        ImVec2(cc.x + ax * r1, cc.y + ay * r1),
                        sc, 1.9f * m.s);
        }
    }

    // ---------- 月亮 ----------
    if (darkA > 0.005f) {
        const float t   = darkA;
        const float rot = -(1.f - darkA) * 1.0471976f;
        const float mr  = rad * 1.18f * (0.60f + 0.40f * t);
        const ImU32 mc  = WithAlpha(ic, t);

        dl->AddCircleFilled(cc, mr, mc, 26);
        dl->AddCircleFilled(cc, mr, mc, 26);

        const float bo = 0.62f * mr;
        const ImVec2 bite(cc.x + cosf(rot - 0.70f) * bo,
                          cc.y + sinf(rot - 0.70f) * bo);
        dl->AddCircleFilled(bite, mr * 0.92f, Mix(p.bgItem, p.bgItemHover, ha), 26);
    }

    // ---------- 切换 ----------
    if (edge && hov) {
        const bool nd = !dark;
        g_cfg.themeIdx = nd ? 0 : 1;
        SetDark(nd);
        themeTransition::Start(nd);
    }
}

} // namespace

// ============================================================
// Frame —— 每帧绘制入口
// ============================================================
bool Frame() {
    const Metrics& m = M();
    const Palette& p = P();
    const ImVec2 disp = ImGui::GetMainViewport()->Size;

    const float now = ImGui::GetTime();
    const float dt  = SafeDt();
    const bool  animOn = F().animEnable;

    // 指针边沿基准必须在所有控件读它之前更新成"上一帧值",
    // 因此这里只把本帧的按下状态存起来, 供本帧内所有控件共享,
    // 帧末再提交。做法是: 控件读 g_prevDown 与本帧实际 down 比较。
    float px = 0.f, py = 0.f;
    bool  pdown = false;
    if (charlie::touch::IsRunning()) {
        charlie::touch::GetPos(&px, &py);
        pdown = charlie::touch::IsDown();
    } else {
        const ImGuiIO& io = ImGui::GetIO();
        px = io.MousePos.x; py = io.MousePos.y; pdown = io.MouseDown[0];
    }
    (void)px; (void)py;

    UpdateStats(dt);

    // ---------- 灵动岛 (常驻) ----------
    {
        IslandData idata;
        if (S().status) idata.statusText = S().status;
        idata.warn = S().warn;
        idata.busy = S().busy;

        static char islandNum[32];
        islandNum[0] = 0;
        idata.valueText = islandNum;
        idata.unitText  = "";

        // 【帧率切走就必须真的消失】
        //   原来的判定是 if(showFps && fpsPos==0) ... else if(showClock),
        //   于是用户选"自由/不显示"后, 岛右侧立刻冒出 HH:MM ——
        //   观感是"帧率被换成了时间", 而不是消失。
        //   现在只看 fpsPos: 只有 0 才填数值, 其余情况保持空串,
        //   岛按空内容重新计算宽度, 自然收缩到"品牌 + 状态"的尺寸。
        if (F().showFps && F().fpsPos == 0) {
            snprintf(islandNum, sizeof(islandNum), "%s", FpsText());
            idata.valueText = islandNum;
            idata.unitText  = "FPS";
        }

        island::SetVisible(true);
        if (island::Draw(idata)) {
            Toggle();
        }
    }

    // ---------- 自由帧率区块 ----------
    freefps::Draw();

    // ---------- 状态机 ----------
    switch (g_state) {
    case WinState::Opening: {
        if (!animOn) {
            g_openProgress = 1.f;
            g_state = WinState::Opened;
        } else {
            g_openProgress = g_openTL.Eval(now, ease::OutQuart);
            if (g_openTL.finished) {
                g_openProgress = 1.f;
                g_state = WinState::Opened;
            }
        }
        break;
    }
    case WinState::Closing: {
        if (!animOn) {
            g_closeProgress = 1.f;
        } else {
            // 闭屏用 InQuart: 起步慢, 末端快速收走
            g_closeProgress = g_closeTL.Eval(now, ease::InQuart);
        }
        if (g_closeProgress >= 1.f) {
            ForceClose();
            SaveConfig();
            g_prevDown = pdown;   // 早退路径也必须提交, 否则下一帧边沿会重复触发
            DrawToasts();
themeTransition::Draw();
            return true;
        }
        break;
    }
    default:
        break;
    }

    // 主面板是否应该绘制
    const bool mainVisible = (g_state != WinState::Hidden);

    // 主面板已隐藏: 岛和 Toast 画完就收工
    if (!mainVisible) {
        g_prevDown = pdown;   // 早退路径同样要提交
        DrawToasts();
themeTransition::Draw();
        return true;
    }

    // ---------- 内容淡入 ----------
    float targetAlpha = (g_state == WinState::Opened) ? 1.f : g_openProgress;
    if (g_state == WinState::Closing) {
        targetAlpha = 1.f - g_closeProgress;
    }
    g_contentAlpha = animOn ? ExpApproach(g_contentAlpha, targetAlpha, 14.f, dt) : targetAlpha;
    if (!animOn) g_contentAlpha = targetAlpha;

    // 灵动岛已在函数开头统一绘制，此处不再重复画。

    // ---------- 窗口几何 ----------
    // 参数对齐参考工程 查理hp源码/jni/src/悬浮窗.cpp:
    //   开屏 0.30s : progress = 1 - (1-t)^4    (OutQuart, 起步猛末端缓)
    //   闭屏 0.22s : progress = 1 - t^4        (InQuart,  起步缓末端猛)
    //   宽度 0 -> 100%
    //   高度 55% -> 100%  (纵向滞后展开, 这是参考工程的关键手感)
    float prog = (g_state == WinState::Closing) ? (1.f - g_closeProgress) : g_openProgress;
    if (!animOn) prog = 1.f;

    const float baseW = m.winW;
    const float baseH = m.winH;
    const float curW  = ImMax(2.f, baseW * prog);
    const float curH  = ImMax(2.f, baseH * (0.55f + 0.45f * prog));

    // 中心 + 拖动偏移
    ImVec2 center(disp.x * 0.5f + g_dragOffset.x, disp.y * 0.5f + g_dragOffset.y);
    ImVec2 winPos(center.x - curW * 0.5f, center.y - curH * 0.5f);

    // ---------- 样式 (按 scale 覆盖基准) ----------
    // 只在度量值变化时写一次。
    // 原来每帧无条件覆盖 8 个字段: ImGuiStyle 是全局对象, 每次赋值
    // 都是 8 次成员写入, 而这些值只在 RebuildMetrics 后才可能变。
    // 用一个缓存指纹判断, 命中就完全跳过。
    {
        const float sc = m.s;
        // 用参与赋值的几个度量拼指纹; 任一变化则整块重写
        const float fingerprint = m.radiusL + m.radius * 7.1f + m.radiusS * 13.3f +
                                  sc * 31.7f + m.knob * 57.1f + m.gap * 91.9f;
        static float s_styleFp = -1.f;
        if (fingerprint != s_styleFp) {
            s_styleFp = fingerprint;
            ImGuiStyle& st = ImGui::GetStyle();
            st.WindowRounding    = m.radiusL;
            st.ChildRounding     = m.radius;
            st.FrameRounding     = m.radiusS;
            st.PopupRounding     = m.radius;
            st.ScrollbarSize     = 5.f * sc;
            st.GrabMinSize       = m.knob;
            st.WindowPadding     = ImVec2(0.f, 0.f);
            st.ItemSpacing       = ImVec2(m.gap * 0.7f, m.gap * 0.55f);
        }
    }

    // ---------- 窗口 ----------
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImClamp(g_contentAlpha, 0.f, 1.f));
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);

    ImGui::Begin("##charlie_main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings);
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();

        // 窗口底 + 描边 + 阴影
        for (int i = 4; i >= 1; --i) {
            float sp = i * 2.4f * m.s;
            dl->AddRectFilled(ImVec2(wp.x - sp, wp.y - sp + 3.f * m.s),
                              ImVec2(wp.x + ws.x + sp, wp.y + ws.y + sp + 3.f * m.s),
                              WithAlpha(p.shadow, 0.10f / i), m.radiusL + sp);
        }
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), p.bgWin, m.radiusL);
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                    WithAlpha(p.border, 0.75f), m.radiusL, 0, 1.2f);

        // 顶部品牌渐变
        dl->AddRectFilledMultiColor(
            ImVec2(wp.x + 1.f, wp.y + 1.f), ImVec2(wp.x + ws.x - 1.f, wp.y + m.titleH * 2.2f),
            WithAlpha(p.accent, 0.07f), WithAlpha(p.accent2, 0.04f),
            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));

        // ---- 侧栏 / 标题栏 / 内容区 ----
        // 导航栏位置: 0=左 (默认) 1=右
        // 布局用 sideOnRight 分流, 内容区始终占另一侧
        const bool sideOnRight = (g_cfg.navSide == 1);
        const float sideW = ImMin(m.sideW, ws.x * 0.34f);

        ImVec2 sidePos = sideOnRight ? ImVec2(wp.x + ws.x - sideW, wp.y)
                                     : ImVec2(wp.x, wp.y);
        ImVec2 titlePos = sideOnRight ? ImVec2(wp.x, wp.y)
                                      : ImVec2(wp.x + sideW, wp.y);

        DrawSidebar(sidePos, ImVec2(sideW, ws.y), sideOnRight);

        // ---- 标题栏 ----
        const float titleH = m.titleH;
        DrawTitleBar(titlePos, ImVec2(ws.x - sideW, titleH));

        // ---- 内容区 ----
        ImVec2 c0 = sideOnRight ? ImVec2(wp.x, wp.y + titleH)
                                : ImVec2(wp.x + sideW, wp.y + titleH);
        ImVec2 c1 = sideOnRight ? ImVec2(wp.x + ws.x - sideW, wp.y + ws.y)
                                : ImVec2(wp.x + ws.x, wp.y + ws.y);
        ImVec2 csize(c1.x - c0.x, c1.y - c0.y);

        ImGui::SetCursorScreenPos(ImVec2(c0.x + m.padX, c0.y + m.padY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ToVec4(p.bgContent));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m.radius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.padX, m.padY));

        // 内容区必须可滚动: 小屏上控件会超出可视区
        ImGui::BeginChild("##content", ImVec2(csize.x - m.padX * 2.f, csize.y - m.padY * 1.4f),
                          false, ImGuiWindowFlags_None);

        // 切页时回到顶部
        if (g_pageResetScroll) {
            ImGui::SetScrollY(0.f);
            g_pageResetScroll = false;
        }

        // ============================================================
        // 触摸拖拽滚动
        //
        // 【旧实现为什么"不跟手"】
        //   1. 启动阈值 6*s (横屏 s=1.55 时接近 10px) 太大 ——
        //      手指先划了 10px 页面纹丝不动, 第一感觉就是"拖不动"。
        //   2. 越过阈值那一帧只加了当帧 MouseDelta,
        //      阈值内已经划掉的那段位移被直接丢弃 -> 画面"嗒"地跳一下。
        //   3. 抬手立刻停止, 完全没有惯性 -> 手感很硬。
        //   4. 旧判据 !IsAnyItemActive() 在触摸下恒为假(卡片里全是
        //      InvisibleButton), 等于整段逻辑从来没跑过。
        //
        // 【现在的做法】
        //   - 阈值降到 3px, 越过即锁存 s_dragging, 并把起步位移补回来
        //   - 拖动期间按 MouseDelta 逐帧跟手
        //   - 抬手按速度做指数衰减惯性, 自然滑停
        //   - 只接管"活动项属于当前滚动窗口"的情况, 不去抢 popup 里的拖动
        // ============================================================
        {
            ImGuiWindow* cw  = ImGui::GetCurrentWindow();
            ImGuiIO&     io  = ImGui::GetIO();
            ImGuiContext& g  = *ImGui::GetCurrentContext();

            const float maxScroll = ImMax(0.f, cw->ContentSize.y - cw->InnerRect.GetHeight());

            // ============================================================
            // 手势状态
            //
            // 【s_armed 是本次修复的核心】
            //   旧实现只在"在内容区内按下"时更新 s_startPos, 但后续
            //   `if (down)` 对整个按下周期都生效, 完全不检查按下位置。
            //   于是出现两个症状:
            //     a) 手指落在悬浮窗外按下 -> s_startPos 还是上一次的旧坐标,
            //        算出的位移必然巨大, 一按就越过阈值, 内容立刻滚走。
            //     b) 手指落在窗口内但按在按钮上 -> 按钮吞掉点击的同时,
            //        滚动逻辑也读同一个 MouseDelta, 页面跟着动。
            //   修法: 增加 s_armed, 只在"按下边沿且落点在内容区内"置位,
            //   抬起或按下在区外时一律为 false, 全程不参与滚动。
            // ============================================================
            static bool   s_dragging = false;
            static bool   s_armed    = false;   // 本次按下是否起于内容区内
            static ImVec2 s_startPos(0.f, 0.f);
            static float  s_velY     = 0.f;     // 惯性速度, px/s

            const bool  down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

            // 起判阈值。
            // 【为什么从 3px 提到 8*s px】
            //   3px 是鼠标量级的值。手指按下时接触面积大、系统还有
            //   抖动补偿前的原始位移, 3px 会被正常点击轻易越过 ->
            //   "点一下按钮, 内容滑了一下"。8*s (横屏约 12px) 才稳。
            const float kSlop = 8.f * m.s;

            // 内容区矩形用窗口自身的 Pos/Size, 不用 InnerRect:
            // InnerRect 会随滚动偏移变化, 用它判断落点会在滚动后失真。
            const ImVec2 wpos = cw->Pos;
            const ImVec2 wsz  = cw->Size;
            const bool inContent =
                (io.MousePos.x >= wpos.x) && (io.MousePos.x <= wpos.x + wsz.x) &&
                (io.MousePos.y >= wpos.y) && (io.MousePos.y <= wpos.y + wsz.y);

            // 按下边沿: 只有落点在内容区内才装填手势
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                s_dragging = false;
                s_velY     = 0.f;
                s_armed    = inContent;
                s_startPos = io.MousePos;
            }

            // 落点不在内容区(含窗口外): 整段手势作废
            if (!inContent && s_dragging) {
                // 已经拖到窗口外时保持拖动 (手指划出边界是常见操作),
                // 这里只在"尚未开始拖动"阶段做拦截, 见下面 if 条件
            }

            if (down && s_armed) {
                if (!s_dragging) {
                    const ImVec2 d(io.MousePos.x - s_startPos.x,
                                   io.MousePos.y - s_startPos.y);
                    // 越过阈值才认定为拖动
                    if (d.x * d.x + d.y * d.y > kSlop * kSlop) {
                        s_dragging = true;
                        // 释放当前 item, 否则 ImGui 继续把移动喂给
                        // 那个 InvisibleButton, 页面不会动。
                        // 只在活动项属于本窗口时才清, 避免误伤 popup。
                        if (g.ActiveId != 0 && !g.ActiveIdIsJustActivated &&
                            g.ActiveIdWindow == cw) {
                            ImGui::ClearActiveID();
                        }
                        // 把阈值内已经划掉的位移补回来, 消除起步跳变
                        cw->Scroll.y -= d.y;
                        s_startPos = io.MousePos;
                        s_velY     = 0.f;
                    }
                } else {
                    cw->Scroll.y -= io.MouseDelta.y;
                    // 记录瞬时速度, 供抬手后做惯性
                    if (io.DeltaTime > 0.0001f) {
                        s_velY = io.MouseDelta.y / io.DeltaTime;
                    }
                }

                // 拖动/补位移后都要钳制
                if (cw->Scroll.y < 0.f)       cw->Scroll.y = 0.f;
                if (cw->Scroll.y > maxScroll) cw->Scroll.y = maxScroll;
            } else if (!down) {
                // 抬起: 释放武装状态, 此后任何 MouseDelta 都不再影响滚动
                s_armed    = false;
                s_dragging = false;
                // 惯性滑动: 指数衰减, 约 1 秒滑停
                if (ImFabs(s_velY) > 2.f) {
                    cw->Scroll.y -= s_velY * io.DeltaTime;
                    // 每帧衰减 ~5%(= 每秒 0.05 的幂), 手感接近系统滚动
                    s_velY *= ImPow(0.05f, io.DeltaTime);
                    if (ImFabs(s_velY) < 2.f) s_velY = 0.f;

                    if (cw->Scroll.y < 0.f)       cw->Scroll.y = 0.f;
                    if (cw->Scroll.y > maxScroll) cw->Scroll.y = maxScroll;
                } else {
                    s_velY = 0.f;
                }
            }
        }

        // 内容区背景 (自己画, 避免 ChildBg 溢出圆角)
        {
            ImVec2 cp = ImGui::GetWindowPos();
            ImVec2 cs = ImGui::GetWindowSize();
            dl->AddRectFilled(cp, ImVec2(cp.x + cs.x, cp.y + cs.y),
                              WithAlpha(p.bgContent, 0.55f), m.radius);
        }

        // 页面内容 (切页淡入)
        g_pageFade = animOn ? ExpApproach(g_pageFade, 1.f, 16.f, dt) : 1.f;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            ImClamp(g_contentAlpha * g_pageFade, 0.f, 1.f));


        DrawPage(g_currentPage);
        ImGui::PopStyleVar();

        // 底部留白, 保证最后一格不贴边
        ImGui::Dummy(ImVec2(1.f, m.padY * 0.6f));

        // ---------- 窗口拖动 (抄 查理hp源码/jni/src/悬浮窗.cpp:3072-3099) ----------
        // 参考实现的条件是 "动画完成 且 免责声明已关", 这里对应
        // prog >= 0.999f (开屏动画收尾) 与 g_disclaimerAgreed。
        // 只在"窗口空白处"按下才起拖: IsAnyItemHovered() 为 true 说明
        // 按在按钮/滑块上, 必须让控件先吃掉这次点击。
        if (prog >= 0.999f && g_disclaimerAgreed) {
            // 【为什么改成"只在标题栏拖"】
            // 原来用 IsWindowHovered(RootAndChildWindows) + !IsAnyItemHovered():
            // 内容区的行都是 InvisibleButton, 手指按在行与行之间的缝隙时
            // IsAnyItemHovered() 可能为 false, 于是这次按下被判成"拖面板",
            // 面板跟着手指走 —— 用户想上下滑内容, 结果整个框在移动。
            //
            // 现在把拖动热区收敛到标题栏(顶部 titleH 内), 内容区一律
            // 交给滚动逻辑。这是移动端悬浮窗的通用做法: 标题栏拖框,
            // 内容区滑内容, 两者不会互相抢。
            const ImVec2 mpNow = ImGui::GetIO().MousePos;
            const bool inTitleBar =
                (mpNow.y >= wp.y) && (mpNow.y <= wp.y + m.titleH) &&
                (mpNow.x >= wp.x) && (mpNow.x <= wp.x + ws.x);

            if (inTitleBar && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                g_dragging       = true;
                g_dragStartMouse = mpNow;
                g_dragStartOffset = g_dragOffset;
            }
            if (g_dragging) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    g_dragOffset.x = g_dragStartOffset.x + (mpNow.x - g_dragStartMouse.x);
                    g_dragOffset.y = g_dragStartOffset.y + (mpNow.y - g_dragStartMouse.y);
                } else {
                    // 松手: 结束拖动并落盘 (参考实现 SaveUiPosition)
                    g_dragging = false;
                    SaveConfig();
                }
            }
            // 限制窗口不超出屏幕 (参考实现 L3090-3096)
            // 以中心为基准, 保证整个窗口始终有交集落在可视区内
            const float minOffX = -disp.x * 0.5f + curW * 0.5f;
            const float maxOffX =  disp.x * 0.5f - curW * 0.5f;
            const float minOffY = -disp.y * 0.5f + curH * 0.5f;
            const float maxOffY =  disp.y * 0.5f - curH * 0.5f;
            // min > max 时 (窗口比屏幕还大) 居中, 避免 ImClamp 行为未定义
            g_dragOffset.x = (minOffX <= maxOffX)
                             ? ImClamp(g_dragOffset.x, minOffX, maxOffX)
                             : 0.f;
            g_dragOffset.y = (minOffY <= maxOffY)
                             ? ImClamp(g_dragOffset.y, minOffY, maxOffY)
                             : 0.f;
        } else {
            g_dragging = false;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // ---------- 免责弹窗 ----------
        //
        // 【为什么必须在 End() 之前调用】
        // 原来这个调用写在 ImGui::End() 之后。End() 之后当前窗口栈为空,
        // ImGui 会把后续所有调用落到它内部自动创建的
        // "Debug##Default" 隐式 fallback 窗口(imgui.cpp:5343)里。
        // 那个窗口只要被塞进任何内容就会以 400x400 渲染出来 ——
        // 这就是用户看到的"除主悬浮窗外还有一个标着 debug 的小悬浮窗"。
        //
        // 弹窗靠 OpenPopupEx 进 g.OpenPopupStack, 层级由 popup 栈决定,
        // 与它是从哪个窗口发起的无关, 放在主窗口内部不会影响它盖在最上层。

        // ---------- 明暗过渡遮罩 ----------
        //
        // 【为什么画在这里】
        //   前一版把调用放在 ImGui::End() 之后, 那时窗口矩形已经拿不到,
        //   函数内部用 GetMainViewport()->Size 兜底, 于是整屏被盖了一层
        //   黑/白蒙版 —— 用户看到的是"手机闪了一下"而不是"面板换了配色"。
        //
        //   明暗切换是悬浮窗自身的属性, 遮罩必须只落在窗口矩形内。
        //   这里在窗口块内调用, 直接传 wp/ws 与窗口圆角 m.radiusL。
        //   窗口隐藏时根本走不到这里 (上面 mainVisible 早返回),
        //   也就不会出现"没有面板却在闪"的情况。
        themeTransition::Draw(wp, ws, m.radiusL);

        DrawDisclaimer(disp);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // ---------- 弹窗关闭后恢复主窗口 hover ----------
    // 【为什么需要这段】
    //   modal 弹窗存在的那些帧, ImGui 会把 g.HoveredWindow 指向弹窗,
    //   并且 modal 期间其它窗口的 hover 全部被屏蔽 —— 这是设计如此。
    //
    //   问题在于关掉 modal 之后: ClosePopupToLevel 会尝试 FocusWindow
    //   恢复焦点, 但它恢复的是 prev_popup.RestoreNavWindow, 而我们的
    //   主窗口用了 NoBringToFrontOnFocus + NoSavedSettings, 并且是在
    //   End() 之后、Debug##Default 隐式窗口上下文里创建弹窗的。
    //   结果是 HoveredWindow 恢复不彻底, 接下来的帧里
    //   imgui.cpp:4593 `if (g.HoveredWindow != window) return false;`
    //   持续否决主面板所有控件的 hover ——
    //   Toggle / Combo / Button 全部"画得出来但点不动"。
    //
    //   修法: 检测到"弹窗刚关闭"这个边沿, 主动 FocusWindow 主窗口,
    //   把 HoveredWindow 与 NavWindow 明确拉回来。
    //
    //   只在边沿触发一次, 不是每帧调用 —— 每帧 FocusWindow 会让
    //   主窗口永远抢焦点, 反而破坏弹窗与下拉菜单的层级。
    {
        static bool s_prevPopupOpen = false;
        const bool popupOpenNow = (ImGui::GetCurrentContext()->OpenPopupStack.Size > 0);

        if (s_prevPopupOpen && !popupOpenNow) {
            // 弹窗刚刚关闭: 把焦点与 hover 拉回主面板
            if (ImGuiWindow* mw = ImGui::FindWindowByName("##charlie_main")) {
                ImGui::FocusWindow(mw);
            }
        }
        s_prevPopupOpen = popupOpenNow;
    }

    // ---------- 帧末提交按下状态 ----------
    // 【这是"明暗按钮连翻、下拉点不开"的总根因】
    //   所有控件的边沿判定都是 edge = tdown && !g_prevDown。
    //   但重建时漏掉了这一句提交, g_prevDown 永远是 false ——
    //   于是手指只要按着, 每一帧 edge 都为真:
    //     · 明暗按钮: 按住不动就每帧 flip 一次主题, 表现为狂闪/失灵
    //     · 下拉表头: TouchEdge 每帧为真, 开->关->开 同帧内互相抵消,
    //                 openNow 状态抖动, 表现为"点了没反应/点不开"
    //   必须在整帧所有控件都读过之后才提交, 位置就在 DrawToasts 之前。
    g_prevDown = pdown;

    // ---------- Toast ----------
    DrawToasts();
    // 明暗过渡遮罩已经在主窗口块内按窗口矩形画过了 (见上面 themeTransition::Draw(wp, ws, ...)),
    // 这里不再重复调用 —— 无参版本是空实现, 留着只会误导后来者以为这里还在画东西。

    return true;
}

} // namespace shell
} // namespace charlie
