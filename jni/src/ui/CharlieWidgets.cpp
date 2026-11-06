// ============================================================
// Charlie UI · 控件库实现
// ============================================================
#include "CharlieWidgets.h"
#include "CharlieTheme.h"
#include "CharlieAnim.h"

// 触摸层原生状态 —— 控件的兜底点击路径要用, 见 Toggle() 里的说明。
#include "../platform/CharlieTouch.h"

// Combo 收起列表要用 ClosePopupToLevel() 与 OpenPopupStack —— 内部 API,
// 只在 imgui_internal.h 里声明 (CharlieShell.cpp 也同样引入)。
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace charlie {
namespace ui {

using namespace charlie;

// ============================================================
// 触摸兜底: 按下边沿 + 一次归属
// ============================================================
// ImGui 的 hover 依赖 g.HoveredWindow == 当前窗口 (imgui.cpp:4593)。
// 弹窗关闭后焦点若没恢复干净, 主面板所有控件会集体"点不动"。
// 这里补一条完全绕开 ImGui 输入管线的判定。
//
// TouchEdge: 全帧共享的按下边沿。同一帧里不管多少控件来问,
//            只有第一帧按下返回 true, 之后的帧返回 false,
//            直到抬起。
// TouchClaim: 一次按下只能被一个控件认领。命中者返回 true,
//            其余控件即使坐标重叠也拿不到, 避免误触发相邻行。
namespace {

bool  g_touchPrevDown = false;
bool  g_touchEdgeFlag = false;      // 本帧是否发生"按下"边沿 (每帧只算一次)
bool  g_touchDownNow  = false;      // 本帧指针是否按下
int   g_touchFrame    = -1;         // 已结算的帧号
const void* g_touchOwner = nullptr; // 本次按下被哪个控件认领

bool  g_touchReleaseFlag = false;    // 本帧是否是"抬起"的那一刻

// 每帧首次调用时结算边沿, 后续调用直接复用结果。
// 不能在每个控件里各自更新 g_touchPrevDown —— 一帧内会遍历几十个
// 控件, 第一个把 prev 改成 down 之后, 其余全部看不到边沿。
void TouchSettleFrame(bool downNow) {
    const int f = ImGui::GetFrameCount();
    if (g_touchFrame == f) return;
    g_touchFrame   = f;
    g_touchDownNow = downNow;
    g_touchEdgeFlag    = (downNow && !g_touchPrevDown);
    g_touchReleaseFlag = (!downNow && g_touchPrevDown);
    g_touchPrevDown = downNow;
    if (!downNow) g_touchOwner = nullptr;   // 抬起后释放归属
}

// 本帧是否是"按下"的那一刻
bool TouchEdge(bool downNow) {
    TouchSettleFrame(downNow);
    return g_touchEdgeFlag;
}

// 本帧是否是"抬起"的那一刻。
// 下拉列表必须用它而不是 TouchEdge:
// 手指按下 Combo 打开列表后通常还停在屏幕上, 接着滑到选项上再抬起。
// 按下瞬间只发生在 Combo 上, 选项处永远收不到 edge,
// 于是"按住拖选"这条路走不通 —— 只能用抬起判定。
bool TouchRelease(bool downNow) {
    TouchSettleFrame(downNow);
    return g_touchReleaseFlag;
}

// 认领本次按下; 已被别人认领则失败
bool TouchClaim(const void* key) {
    if (g_touchOwner != nullptr) return false;
    g_touchOwner = key;
    return true;
}

} // namespace

static inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ============================================================
// 布局
// ============================================================
// ------------------------------------------------------------
// 按可用宽度把文字截断成 "...".
//
// 起因(用户报的"字容器内文字/按键被遮挡, 无法完全显示"):
//   仪表页、样式卡片里的标签是「标签: 值」拼在一行, 例如
//   "导航栏位置: 左侧"。竖屏/横屏收窄后, 这一行比控件还宽,
//   ImGui 的 AddText 不做裁切, 于是文字直接画到旁边的下拉框、
//   开关上面, 看起来就是"被遮挡"。
//
// 这里做两件事:
//   1) 二分找出在 maxW 内最多能放几个字符(等宽估算不可靠, 直接用 CalcTextSize)
//   2) 保留前缀 + "…", 保证语义还看得懂
// 返回写入 out 的字符串, 调用方直接用。
// ------------------------------------------------------------
const char* Ellipsize(const char* src, float maxW, char* out, int outCap) {
    if (!out || outCap <= 1) return src;
    if (!src) { out[0] = '\0'; return out; }

    const ImVec2 full = ImGui::CalcTextSize(src);
    if (full.x <= maxW || maxW <= 0.f) {
        snprintf(out, (size_t)outCap, "%s", src);
        return out;
    }

    // "…" 本身也要占宽
    static const char* kDot = "...";
    const float dotW = ImGui::CalcTextSize(kDot).x;
    const float avail = maxW - dotW;
    if (avail <= 0.f) { snprintf(out, (size_t)outCap, "%s", kDot); return out; }

    // src 是 UTF-8, 逐字节试会切坏汉字 —— 先按 UTF-8 码点切开
    int cps[128];      // 每个码点的起始字节下标
    int n = 0;
    for (int i = 0; src[i] && n < 127; ++n) {
        cps[n] = i;
        const unsigned char c = (unsigned char)src[i];
        if      (c < 0x80) i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else               i += 4;
    }
    cps[n] = (int)strlen(src);   // 末尾哨兵

    // 二分找最大可容纳的码点数
    int lo = 0, hi = n;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        char tmp[256];
        const int len = cps[mid];
        if (len >= 255) { hi = mid - 1; continue; }
        memcpy(tmp, src, (size_t)len);
        tmp[len] = '\0';
        if (ImGui::CalcTextSize(tmp).x <= avail) lo = mid; else hi = mid - 1;
    }

    const int keep = cps[lo];
    const int copy = (keep < outCap - 4) ? keep : (outCap - 4);
    if (copy > 0) memcpy(out, src, (size_t)copy);
    out[copy > 0 ? copy : 0] = '\0';
    strncat(out, kDot, (size_t)(outCap - strlen(out) - 1));
    return out;
}

void Gap(float h) {
    if (h < 0.f) h = M().gap;
    ImGui::Dummy(ImVec2(1.f, h));
}

void Spacer(float w) {
    ImGui::SameLine(0.f, w);
}

void Section(const char* title) {
    const Metrics& m = M();
    const Palette& p = P();
    ImGui::Dummy(ImVec2(1.f, m.gapS * 0.5f));

    ImVec2 cp = ImGui::GetCursorScreenPos();
    const float h  = m.fontS * 1.5f;
    const float bw = 3.f * m.s;

    // 左侧强调竖条
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(cp.x, cp.y + h * 0.16f),
        ImVec2(cp.x + bw, cp.y + h * 0.84f),
        p.accent, bw * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(cp.x + bw + m.gapS, cp.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();

    // 右侧渐隐延长线
    ImVec2 after = ImGui::GetCursorScreenPos();
    float lineY  = cp.y + h * 0.5f;
    float lineX0 = after.x + m.gapS;
    float lineX1 = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - m.padX;
    if (lineX1 > lineX0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(
            ImVec2(lineX0, lineY), ImVec2(lineX1, lineY + 1.f),
            WithAlpha(p.divider, 0.9f), WithAlpha(p.divider, 0.0f),
            WithAlpha(p.divider, 0.0f), WithAlpha(p.divider, 0.9f));
    }
    ImGui::Dummy(ImVec2(1.f, m.gapS * 0.6f));
}

void Label(const char* text, bool sub) {
    const Metrics& m = M();
    const Palette& p = P();
    if (sub) ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(WithAlpha(p.textMid, 0.92f)));
    else     ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
    ImGui::SetWindowFontScale(sub ? 0.88f : 1.f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    (void)m;
}

void Divider() {
    const Metrics& m = M();
    const Palette& p = P();
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(1.f, m.gapS * 0.5f));
    cp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(cp.x, cp.y), ImVec2(cp.x + avail, cp.y + 1.f),
        WithAlpha(p.divider, 0.75f));
    ImGui::Dummy(ImVec2(1.f, m.gapS * 0.5f));
}

// ============================================================
// 卡片
//
// 【为什么改成自适应高度】
// 原来 BeginCard 接收一个"手算出来的 height", 调用点靠
//   cardH = cardPad*2 + (rowH+1)*行数
// 硬推。只要行高发生变化 —— 例如带说明(hint)的行需要两行文字、
// 系统字号放大、横竖屏切换导致 s 变化 —— 算出来的 height 就偏小,
// BeginChild 会按固定高度裁切, 卡片里最后一两行控件被直接切掉,
// 用户看到的就是"按键被遮挡 / 显示不全"。
//
// 现在: height <= 0 表示自适应。
// 自适应时把高度交给 ImGui (传 0 = 由内容决定), 背景改用
// ImGuiCol_ChildBg + ChildBorderSize 让 ImGui 自己画 —— 这很关键:
// 手动 AddRectFilled 只能在内容之后画, 会盖住内容;
// 而 ChildBg 是 ImGui 在绘制内容前的背景层, 顺序天然正确。
// 传入正数时保持旧行为, 调用点可以逐步迁移。
// ============================================================
namespace { struct CardAutoState { bool autoH; }; 
             CardAutoState g_cardAuto[8]; int g_cardDepth = 0; }

bool BeginCard(const char* id, float height) {
    const Metrics& m = M();
    const Palette& p = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    const bool autoH = (height <= 0.f);

    if (g_cardDepth < 8) g_cardAuto[g_cardDepth++] = { autoH };

    if (!autoH) {
        // 固定高度: 保持旧行为, 先画底再画内容
        dl->AddRectFilled(cp, ImVec2(cp.x + w, cp.y + height), p.bgCard, m.radius);
        dl->AddRect(cp, ImVec2(cp.x + w, cp.y + height), WithAlpha(p.border, 0.55f),
                    m.radius, 0, 1.f);
    }

    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.cardPad, cp.y + m.cardPad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    // 【关键】自适应高度必须用 ImGuiChildFlags_AutoResizeY。
    //
    // 上一版把 size.y 传 0 —— 那是"占满父容器剩余高度"的意思,
    // 不是"按内容收缩"。结果卡片子容器把整页撑满, 同页后面的卡片
    // 全被挤出可视区, 用户看到的正是"子容器太大, 把其他控件挤出去了"。
    //
    // AutoResizeY 才是"高度由内容决定":
    //   - 内容少 -> 卡片就矮
    //   - 内容多 -> 卡片变高, 页面整体可滚动
    // 同时宽度仍按父容器给定, 不会横向乱跳。
    if (autoH) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ToVec4(p.bgCard));
        ImGui::PushStyleColor(ImGuiCol_Border, ToVec4(WithAlpha(p.border, 0.55f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m.radius);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
        // Borders 会隐式打开 WindowPadding, 而我们已按 cardPad 手动定位,
        // 再叠一层内边距会多缩一圈。用 AlwaysUseWindowPadding 把
        // WindowPadding 压成 0(上一行已 Push), 内边距只由 cardPad 负责。
        ImGui::BeginChild(id, ImVec2(w - m.cardPad * 2.f, 0.f),
                          ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_Borders |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    } else {
        const float childH = height - m.cardPad * 2.f;
        ImGui::BeginChild(id, ImVec2(w - m.cardPad * 2.f, childH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }
    ImGui::PopStyleVar();
    if (autoH) {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    // 注意: 这里不能塞 Dummy —— AutoResizeY 靠测量内容末尾的
    // CursorPos 决定高度, 多余的 Dummy 会把卡片底多撑一截。
    // 点击穿透已由 Child 本身不吃空白区保证。
    return true;
}

void EndCard() {
    const Metrics& m = M();

    bool autoH = false;
    if (g_cardDepth > 0) autoH = g_cardAuto[--g_cardDepth].autoH;

    ImGui::EndChild();
    // Child 已消费了高度, 这里只补间距
    Gap(m.gapS);
    (void)autoH;
}

// ============================================================
// 开关
// ============================================================
namespace {

// 画一个开关轨道 + 圆钮, 返回占用的绘制矩形
// ============================================================
// 明暗交互按键
//
// 视觉规则（纯黑白灰, 无色相）:
//   关 = 暗   : 轨道深灰(c26), 描边暗(c32), 钮灰(c72)
//   开 = 亮   : 轨道白(cFF), 描边白, 钮黑(c00)  —— 反相, 对比最强
//   悬停      : 整体提亮一档
//   按下      : 再提亮一档 + 轻微内缩
//
// a = 0..1 的动画进度, 由 Anim() 平滑推进, 所以明暗切换是渐变不是跳变。
// ============================================================
void DrawSwitch(ImDrawList* dl, ImVec2 pos, float tw, float th, float a,
                ImU32 onCol, ImU32 offCol) {
    const Metrics& m = M();
    const Palette& p = P();

    ImVec2 a0 = pos;
    ImVec2 a1 = ImVec2(pos.x + tw, pos.y + th);
    const float r = th * 0.5f;

    // 轨道: 暗 -> 亮
    const ImU32 trackCol = Mix(offCol, onCol, a);
    dl->AddRectFilled(a0, a1, trackCol, r);

    // 描边: 关态几乎不可见, 开态亮白, 强化"亮"的感觉
    dl->AddRect(a0, a1,
                Mix(WithAlpha(p.border, 0.9f), WithAlpha(grayc(255), 0.85f), a),
                r, 0, 1.2f * m.s);

    // 开启时的顶部高光（白色柔光, 只加亮度不加色）
    if (a > 0.02f) {
        dl->AddRectFilled(ImVec2(a0.x + 1.f, a0.y + 1.f),
                          ImVec2(a1.x - 1.f, a0.y + th * 0.46f),
                          WithAlpha(grayc(255), 0.22f * a), r);
    }

    // 圆钮: 关=浅灰, 开=纯黑（反相）
    const float kr   = (th - 6.f * m.s) * 0.5f;
    const float pad  = 3.f * m.s;
    const float cx   = a0.x + pad + kr + (tw - pad * 2.f - kr * 2.f) * a;
    const float cy   = a0.y + th * 0.5f;

    // 钮影（开态更明显, 因为钮变黑了需要边界）
    dl->AddCircleFilled(ImVec2(cx, cy + 1.2f * m.s), kr,
                        WithAlpha(grayc(0), 0.35f + 0.25f * a), 24);

    // 钮本体: 关=灰72, 开=黑0
    const ImU32 knobCol = Mix(grayc(184), grayc(0), a);
    dl->AddCircleFilled(ImVec2(cx, cy), kr, knobCol, 24);

    // 开态钮上加一圈细亮边, 让黑钮在白轨上不糊
    if (a > 0.25f) {
        dl->AddCircle(ImVec2(cx, cy), kr,
                      WithAlpha(grayc(255), 0.30f * a), 24, 1.f * m.s);
    }
}

} // namespace

bool Toggle(const char* label, bool* v) {
    const Metrics& m = M();
    const Palette& p = P();

    const float tw = 46.f * m.s;
    const float th = 26.f * m.s;
    const float rh = m.rowH;

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float rowW = ImGui::GetContentRegionAvail().x;

    ImGui::InvisibleButton("##row", ImVec2(rowW, rh));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(0);

    // ---------- 触摸兜底 ----------
    // ImGui 的 hover 依赖 g.HoveredWindow == 当前窗口 (imgui.cpp:4593)。
    // 弹窗关闭后若焦点没恢复干净, 主面板所有控件会集体"点不动"。
    // 这里用触摸层原生状态补一条, 只要按下点落在本行矩形内就算点击。
    if (!clicked) {
        float tx = 0.f, ty = 0.f;
        bool  tdown = false;
        if (charlie::touch::IsRunning()) {
            charlie::touch::GetPos(&tx, &ty);
            tdown = charlie::touch::IsDown();
        } else {
            const ImGuiIO& dio = ImGui::GetIO();
            tx = dio.MousePos.x;  ty = dio.MousePos.y;  tdown = dio.MouseDown[0];
        }
        const bool inRow = (tx >= cp.x && tx <= cp.x + rowW &&
                            ty >= cp.y && ty <= cp.y + rh);

        // 按下边沿: 全局只认一次, 由"哪个控件在下落沿被命中"决定归属。
        // 用文件级状态而不是本控件 static —— 一帧内会遍历多个 Toggle,
        // 各自 static 会让第一个命中后其余全部误判。
        const bool edge = TouchEdge(tdown);
        if (edge && inRow && TouchClaim(label)) clicked = true;
        if (inRow) hovered = true;
    }

    if (clicked && v) { *v = !*v; }

    const float a = Anim(v, v ? *v : false, 14.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 行悬停底
    if (hovered) {
        dl->AddRectFilled(cp, ImVec2(cp.x + rowW, cp.y + rh),
                          WithAlpha(p.bgItemHover, 0.55f), m.radiusS);
    }

    // 文字
    ImU32 tc = (v && *v) ? p.textHi : p.textMid;
    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, cp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(tc));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // 轨道
    DrawSwitch(dl, ImVec2(cp.x + rowW - tw, cp.y + (rh - th) * 0.5f), tw, th, a,
               p.accent, p.track);

    ImGui::PopID();
    return clicked && v;
}

bool Toggle2(const char* l1, bool* v1, const char* l2, bool* v2) {
    const Metrics& m = M();
    float full = ImGui::GetContentRegionAvail().x;
    float colW = (full - m.gap) * 0.5f;

    ImGui::BeginGroup();
    bool c1 = Toggle(l1, v1);
    ImGui::EndGroup();
    ImGui::SameLine(0.f, m.gap);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(colW);
    bool c2 = Toggle(l2, v2);
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    return c1 || c2;
}

bool Toggle4(const char* l1, bool* v1, const char* l2, bool* v2,
             const char* l3, bool* v3, const char* l4, bool* v4) {
    const Metrics& m = M();
    float full = ImGui::GetContentRegionAvail().x;
    float colW = (full - m.gap * 3.f) * 0.25f;

    bool ch = false;
    // 紧凑开关: 4 个等分, 每个只留开关 + 上下两行文字
    struct Item { const char* label; bool* v; };
    Item items[4] = {{l1, v1}, {l2, v2}, {l3, v3}, {l4, v4}};

    const Palette& p = P();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const float cellH = m.rowH * 1.55f;

    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine(0.f, m.gap);
        ImGui::BeginGroup();
        ImGui::PushID(i);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##c4", ImVec2(colW, cellH));
        bool hov = ImGui::IsItemHovered();
        bool clk = ImGui::IsItemClicked(0);
        if (clk && items[i].v) { *items[i].v = !*items[i].v; ch = true; }

        bool on = items[i].v ? *items[i].v : false;
        float a = Anim(items[i].v, on, 14.f);

        if (hov) {
            dl->AddRectFilled(cp, ImVec2(cp.x + colW, cp.y + cellH),
                              WithAlpha(p.bgItemHover, 0.5f), m.radiusS);
        }

        // 顶部小开关 (居中)
        const float tw = 34.f * m.s;
        const float th = 19.f * m.s;
        DrawSwitch(dl, ImVec2(cp.x + (colW - tw) * 0.5f, cp.y + m.gapS * 0.8f),
                   tw, th, a, p.accent, p.track);

        // 底部文字 (居中, 单行截断)
        const char* txt = items[i].label ? items[i].label : "";
        ImVec2 ts = ImGui::CalcTextSize(txt);
        float tx = cp.x + (colW - ts.x) * 0.5f;
        if (tx < cp.x + 2.f) tx = cp.x + 2.f;
        dl->PushClipRect(cp, ImVec2(cp.x + colW, cp.y + cellH), true);
        dl->AddText(ImVec2(tx, cp.y + cellH - ts.y - m.gapS * 0.7f),
                    on ? p.textHi : p.textMid, txt);
        dl->PopClipRect();

        ImGui::PopID();
        ImGui::EndGroup();
    }
    (void)rowStart;
    return ch;
}

bool Check(const char* label, bool* v) {
    const Metrics& m = M();
    const Palette& p = P();
    const float box = 20.f * m.s;
    const float rh  = m.rowH;

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float rowW = ImGui::GetContentRegionAvail().x;

    ImGui::InvisibleButton("##chk", ImVec2(rowW, rh));
    bool hov = ImGui::IsItemHovered();
    bool clk = ImGui::IsItemClicked(0);
    if (clk && v) *v = !*v;

    bool on = v ? *v : false;
    float a = Anim(v, on, 16.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) {
        dl->AddRectFilled(cp, ImVec2(cp.x + rowW, cp.y + rh),
                          WithAlpha(p.bgItemHover, 0.5f), m.radiusS);
    }

    ImVec2 b0(cp.x + m.gapS, cp.y + (rh - box) * 0.5f);
    ImVec2 b1(b0.x + box, b0.y + box);
    dl->AddRectFilled(b0, b1, Mix(p.bgItem, p.accent, a), 5.f * m.s);
    dl->AddRect(b0, b1, Mix(p.border, p.accent, a), 5.f * m.s, 0, 1.4f);

    if (a > 0.05f) {
        ImU32 tick = WithAlpha(IM_COL32(255, 255, 255, 255), a);
        ImVec2 c = ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
        float u = box * 0.26f;
        dl->AddLine(ImVec2(c.x - u, c.y), ImVec2(c.x - u * 0.15f, c.y + u * 0.85f),
                    tick, 2.2f * m.s);
        dl->AddLine(ImVec2(c.x - u * 0.15f, c.y + u * 0.85f), ImVec2(c.x + u, c.y - u * 0.8f),
                    tick, 2.2f * m.s);
    }

    ImGui::SetCursorScreenPos(ImVec2(b1.x + m.gap, cp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(on ? p.textHi : p.textMid));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    ImGui::PopID();
    return clk && v;
}

// ============================================================
// 滑块
// ============================================================
bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt) {
    const Metrics& m = M();
    const Palette& p = P();
    if (!v) return false;

    const float h      = m.rowH + m.gapS * 1.6f;
    const float trackY = m.rowH * 0.78f;

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    ImGui::InvisibleButton("##sl", ImVec2(w, h));
    bool active = ImGui::IsItemActive();
    bool hov    = ImGui::IsItemHovered();

    bool changed = false;
    if (active) {
        float t = (ImGui::GetIO().MousePos.x - cp.x) / (w > 0.f ? w : 1.f);
        t = ClampF(t, 0.f, 1.f);
        float nv = lo + (hi - lo) * t;
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;

    float t = (hi - lo) != 0.f ? (*v - lo) / (hi - lo) : 0.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 标签
    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, cp.y + 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // 数值 (右对齐, 强调色)
    char buf[48];
    snprintf(buf, sizeof(buf), fmt ? fmt : "%.0f", *v);
    ImVec2 ns = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(cp.x + w - ns.x - m.gapS, cp.y + 1.f),
                (active || hov) ? p.accentText : p.accent, buf);

    // 轨道
    ImVec2 t0(cp.x + m.gapS, cp.y + trackY);
    ImVec2 t1(cp.x + w - m.gapS, cp.y + trackY + m.trackH);
    float rad = m.trackH * 0.5f;
    dl->AddRectFilled(t0, t1, p.track, rad);
    float fx = t0.x + (t1.x - t0.x) * t;
    if (fx > t0.x + rad) {
        dl->AddRectFilled(t0, ImVec2(fx, t1.y), p.accent, rad);
    }

    // 圆钮
    float knobR = 9.f * m.s;
    if (active) knobR *= 1.22f;
    ImVec2 knob(fx, (t0.y + t1.y) * 0.5f);
    dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.f), knobR + 1.f, IM_COL32(0, 0, 0, 60), 20);
    dl->AddCircleFilled(knob, knobR, IM_COL32(250, 252, 250, 255), 20);
    if (active) {
        dl->AddCircle(knob, knobR + 4.f * m.s, WithAlpha(p.accent, 0.35f), 20, 2.f * m.s);
    }

    ImGui::PopID();
    return changed;
}

bool SliderInt(const char* label, int* v, int lo, int hi) {
    if (!v) return false;
    float f = (float)*v;
    bool ch = SliderFloat(label, &f, (float)lo, (float)hi, "%.0f");
    int nv = (int)(f + 0.5f);
    if (nv < lo) nv = lo;
    if (nv > hi) nv = hi;
    if (nv != *v) { *v = nv; return true; }
    return ch;
}

bool SliderCompact(const char* label, float* v, float lo, float hi,
                   const char* fmt, float width) {
    const Metrics& m = M();
    const Palette& p = P();
    if (!v) return false;

    const float h      = m.rowH;
    const float labelW = ImGui::CalcTextSize(label).x;

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##slc", ImVec2(width, h));
    bool active = ImGui::IsItemActive();

    const float tx0 = cp.x + labelW + m.gapS;
    const float tw  = width - labelW - m.gapS - 3.f;
    bool changed = false;
    if (active && tw > 4.f) {
        float t = (ImGui::GetIO().MousePos.x - tx0) / tw;
        t = ClampF(t, 0.f, 1.f);
        float nv = lo + (hi - lo) * t;
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (*v < lo) *v = lo;
    if (*v > hi) *v = hi;
    float t = (hi - lo) != 0.f ? (*v - lo) / (hi - lo) : 0.f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float baseY = cp.y + h * 0.5f;

    dl->AddText(ImVec2(cp.x, baseY - ImGui::GetTextLineHeight() * 0.5f), p.textMid, label);

    float rad = m.trackH * 0.5f * 0.75f;
    dl->AddRectFilled(ImVec2(tx0, baseY - rad), ImVec2(tx0 + tw, baseY + rad), p.track, rad);
    float fx = tx0 + tw * t;
    if (fx > tx0 + rad) {
        dl->AddRectFilled(ImVec2(tx0, baseY - rad), ImVec2(fx, baseY + rad), p.accent, rad);
    }
    dl->AddCircleFilled(ImVec2(fx, baseY), 6.f * m.s, IM_COL32(250, 252, 250, 255), 16);

    // 数值贴在右侧 (滑块下方无空间, 叠在标签后面)
    char buf[32];
    snprintf(buf, sizeof(buf), fmt ? fmt : "%.0f", *v);
    ImVec2 ns = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(cp.x + width - ns.x, baseY - ns.y * 0.5f), p.accent, buf);

    ImGui::PopID();
    return changed;
}

// ============================================================
// 下拉 —— 浮层覆盖实现 (外连样式)
//
// 【历史两版的取舍】
//   v1: ImGui::OpenPopup/BeginPopup  —— 触摸注入下 popup 被 NewFrame 的
//       ClosePopupsOverWindow 依据 HoveredId 修剪, 手指一抬就关, "闪一下"。
//   v2: 内联展开 + Dummy 撑高       —— 稳定了, 但选项会把下方控件挤开,
//       用户滚动位置被动改变, 视觉上像"整个面板跳了一下"。
//   v3(本版): 自绘浮层。命中判定仍由坐标全权掌握(稳定),
//       但不写任何 ImGui item, 不改变光标, 不撑高容器 -> 纯覆盖。
//
// 【层级怎么保证】
//   浮层用 ImGui::GetForegroundDrawList() 绘制。
//   背景 drawlist 每帧先输出, 前景最后输出, 所以浮层天然压在所有面板
//   内容之上, 不需要额外处理 z-order。
//
// 【为什么不放在 BeginChild 内部就画】
//   内容区是可滚动 Child, 它的 drawlist 有 ClipRect。
//   在 Child 内直接画 AddRectFilled 会被裁到 Child 边界,
//   浮层超出内容区下半部分就消失。所以:
//     - 记下展开状态与锚点矩形, 画到前景 drawlist (无裁剪)
//     - 命中判定仍在本帧按坐标做, 与视觉完全一致
// ============================================================

// 当前展开的 Combo 全局状态 (同一时刻只允许一个)
static ImGuiID g_openComboId  = 0;
static ImVec2  g_openComboPos(0.f, 0.f);   // 浮层左上角
static ImVec2  g_openComboSize(0.f, 0.f);  // 浮层尺寸

bool Combo(const char* label, int* idx, const char* const items[], int count) {
    const Metrics& m = M();
    const Palette& p = P();
    if (!idx || !items || count <= 0) return false;
    if (!label) label = "";

    // label 允许为空串 (RowCombo 在外层已经单独画了行标签)。
    // PushID("") 仍然会叠一层 ID 栈, 配合下面的 GetID("##cb"),
    // 不同行的下拉依然互不冲突, 因为外层 RowCombo 已 PushID(唯一标签)。
    ImGui::PushID(label);
    const ImGuiID myId = ImGui::GetID("##cb");

    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    const float rh = m.rowH;

    // ---------- 指针状态 ----------
    float tx = 0.f, ty = 0.f;
    bool  tdown = false;
    if (charlie::touch::IsRunning()) {
        charlie::touch::GetPos(&tx, &ty);
        tdown = charlie::touch::IsDown();
    } else {
        const ImGuiIO& dio = ImGui::GetIO();
        tx = dio.MousePos.x;  ty = dio.MousePos.y;  tdown = dio.MouseDown[0];
    }
    const bool inField = (tx >= cp.x && tx <= cp.x + w &&
                          ty >= cp.y && ty <= cp.y + rh);

    const bool isOpen = (g_openComboId == myId);

    // ---------- 点表头: 开 / 关 ----------
    // 用按下边沿而不是 IsItemClicked: 后者在抬起帧也可能为真, 会一开一关连抖。
    if (TouchEdge(tdown) && inField && TouchClaim((const void*)(intptr_t)0x8FF0)) {
        if (isOpen) {
            g_openComboId = 0;
        } else {
            g_openComboId = myId;
            // 浮层锚点: 表头下方, 宽度与表头一致
            g_openComboPos  = ImVec2(cp.x, cp.y + rh + 2.f * m.s);
            const float itemH = m.rowH * 0.92f;
            g_openComboSize = ImVec2(w, itemH * (float)count + m.gapS * 2.f);
        }
    }

    const bool openNow = (g_openComboId == myId);

    // ---------- 表头 ----------
    ImGui::InvisibleButton("##cbhdr", ImVec2(w, rh));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cp, ImVec2(cp.x + w, cp.y + rh),
                      (inField || openNow) ? p.bgItemHover : p.bgItem, m.radiusS);
    dl->AddRect(cp, ImVec2(cp.x + w, cp.y + rh),
                WithAlpha(openNow ? p.accent : p.border, openNow ? 0.85f : 0.6f),
                m.radiusS, 0, openNow ? (1.6f * m.s) : 1.f);

    // 【标签里的 "##" 必须剥掉】
    //   RowCombo 传进来的 label 是 "##c" —— 那只是给 PushID/GetID 用的唯一化后缀,
    //   不是给人看的文字。原来直接 snprintf("%s: %s", label, ...), 表头就渲染成
    //   "##c: 灵动岛内", 把 ImGui 的内部标记漏到了界面上。
    //   ImGui 原生控件的规矩是 "显示文字##唯一ID", 这里补上同样的处理:
    //   取 "##" 之前的可见部分, 没有 "##" 就整串都是可见部分。
    char visLabel[128];
    {
        const char* hash = strstr(label, "##");
        const size_t n = hash ? (size_t)(hash - label) : strlen(label);
        const size_t cap = sizeof(visLabel) - 1;
        size_t k = (n < cap) ? n : cap;
        memcpy(visLabel, label, k);
        visLabel[k] = 0;
    }

    char buf[256];
    if (visLabel[0]) {
        snprintf(buf, sizeof(buf), "%s: %s", visLabel,
                 items[(*idx >= 0 && *idx < count) ? *idx : 0]);
    } else {
        // 没有可见标签时只显示当前项, 不留下 ": xxx" 前面那个孤零零的冒号
        snprintf(buf, sizeof(buf), "%s",
                 items[(*idx >= 0 && *idx < count) ? *idx : 0]);
    }
    const float textMaxW = w - m.gap * 2.f - m.gap * 1.2f - 6.f * m.s;
    char trimmed[256];
    Ellipsize(buf, textMaxW, trimmed, (int)sizeof(trimmed));
    ImVec2 ts = ImGui::CalcTextSize(trimmed);
    dl->AddText(ImVec2(cp.x + m.gap, cp.y + (rh - ts.y) * 0.5f),
                openNow ? p.accent : p.textHi, trimmed);

    // 箭头: 展开时翻转朝上
    const float ax = cp.x + w - m.gap * 1.2f;
    const float ay = cp.y + rh * 0.5f;
    const float as = 4.f * m.s;
    if (openNow) {
        dl->AddTriangleFilled(ImVec2(ax - as, ay + as * 0.5f),
                              ImVec2(ax + as, ay + as * 0.5f),
                              ImVec2(ax, ay - as * 0.8f), p.accent);
    } else {
        dl->AddTriangleFilled(ImVec2(ax - as, ay - as * 0.5f),
                              ImVec2(ax + as, ay - as * 0.5f),
                              ImVec2(ax, ay + as * 0.8f), p.textMid);
    }

    bool changed = false;

    if (openNow) {
        // ---------- 浮层: 前景 drawlist, 无裁剪, 覆盖一切 ----------
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 o0 = g_openComboPos;
        const ImVec2 o1(o0.x + g_openComboSize.x, o0.y + g_openComboSize.y);

        // 投影: 浮层脱离平面, 需要阴影把它"抬起来"
        for (int i = 4; i >= 1; --i) {
            const float sp = i * 2.2f * m.s;
            fg->AddRectFilled(ImVec2(o0.x - sp, o0.y - sp + 2.f * m.s),
                              ImVec2(o1.x + sp, o1.y + sp + 2.f * m.s),
                              WithAlpha(p.shadow, 0.11f / (float)(i * i)),
                              m.radiusS + sp);
        }

        // 浮层底: 不透明, 必须完全盖住下面的控件
        const bool dark = IsDark();
        const ImU32 sheetT = dark ? IM_COL32(38, 38, 40, 252) : IM_COL32(255, 255, 255, 253);
        const ImU32 sheetB = dark ? IM_COL32(24, 24, 26, 252) : IM_COL32(247, 247, 247, 253);
        fg->AddRectFilledMultiColor(o0, o1, sheetT, sheetT, sheetB, sheetB);
        fg->AddRect(o0, o1, WithAlpha(p.accent, 0.55f), m.radiusS, 0,
                    ImMax(1.f, m.s * 0.9f));

        // 逐项
        const float itemH = m.rowH * 0.92f;
        float iy = o0.y + m.gapS;
        for (int i = 0; i < count; ++i) {
            const ImVec2 i0(o0.x + m.gapS * 0.5f, iy);
            const ImVec2 i1(o1.x - m.gapS * 0.5f, iy + itemH);
            const bool inItem = (tx >= i0.x && tx <= i1.x &&
                                 ty >= i0.y && ty <= i1.y);
            const bool sel = (i == *idx);

            if (inItem) {
                fg->AddRectFilled(i0, i1, p.bgItemHover, m.radiusS * 0.7f);
            }
            if (sel) {
                fg->AddRectFilled(ImVec2(i0.x, i0.y + itemH * 0.22f),
                                  ImVec2(i0.x + 2.6f * m.s, i1.y - itemH * 0.22f),
                                  p.accent, 1.3f * m.s);
            }

            char ibuf[256];
            Ellipsize(items[i], (i1.x - i0.x) - m.gap * 2.f, ibuf, (int)sizeof(ibuf));
            ImVec2 isz = ImGui::CalcTextSize(ibuf);
            fg->AddText(ImVec2(i0.x + m.gapS, i0.y + (itemH - isz.y) * 0.5f),
                        sel ? p.accent : p.textHi, ibuf);

            // 两条边沿都收: 直接点选项 / 按住表头拖过来再抬
            if (inItem && (TouchEdge(tdown) || TouchRelease(tdown)) &&
                TouchClaim((const void*)(intptr_t)(0x9000 + i))) {
                *idx = i;
                changed = true;
                g_openComboId = 0;
            }
            iy += itemH;
        }

        // 点外部收起 (选项优先于外部)
        const bool inHeader = inField;
        const bool inSheet  = (tx >= o0.x && tx <= o1.x &&
                               ty >= o0.y && ty <= o1.y);
        if (!changed && TouchEdge(tdown) && !inHeader && !inSheet) {
            g_openComboId = 0;
        }
    }

    ImGui::PopID();
    return changed;
}

// 供外部在面板关闭 / 切页时强制收起
void CloseOpenCombo() { g_openComboId = 0; }
// ============================================================
// 按钮
// ============================================================
bool Button(const char* label, BtnKind kind, float width) {
    const Metrics& m = M();
    const Palette& p = P();

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
    const float h = m.rowH * 0.96f;

    ImGui::InvisibleButton("##btn", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    bool clk = ImGui::IsItemClicked(0);

    // 触摸兜底 (说明见 Toggle)
    if (!clk) {
        float tx = 0.f, ty = 0.f;
        bool  tdown = false;
        if (charlie::touch::IsRunning()) {
            charlie::touch::GetPos(&tx, &ty);
            tdown = charlie::touch::IsDown();
        } else {
            const ImGuiIO& dio = ImGui::GetIO();
            tx = dio.MousePos.x;  ty = dio.MousePos.y;  tdown = dio.MouseDown[0];
        }
        const bool inBtn = (tx >= cp.x && tx <= cp.x + w &&
                            ty >= cp.y && ty <= cp.y + h);
        if (TouchEdge(tdown) && inBtn && TouchClaim(label)) clk = true;
        if (inBtn) hov = true;
    }

    float a = Anim(label, hov || held, 16.f);
    if (held) a = 1.f;

    ImU32 bg, fg, bd;
    switch (kind) {
    case BtnKind::Primary:
        bg = Mix(p.accentDim, p.accent, a);
        fg = p.textOnAccent;
        bd = WithAlpha(p.accent, 0.9f);
        break;
    case BtnKind::Danger:
        bg = Mix(p.bgItem, p.danger, 0.28f + a * 0.5f);
        fg = (a > 0.4f) ? IM_COL32(255, 255, 255, 255) : p.danger;
        bd = WithAlpha(p.danger, 0.55f + a * 0.35f);
        break;
    default: // Ghost
        bg = Mix(p.bgItem, p.bgItemHover, a);
        fg = p.textHi;
        bd = WithAlpha(p.border, 0.65f);
        break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b0 = cp, b1 = ImVec2(cp.x + w, cp.y + h);

    // 按下时轻微下沉, 给触摸一个物理反馈
    if (held) { b0.y += 1.f; b1.y += 1.f; }

    dl->AddRectFilled(b0, b1, bg, m.radiusS);
    dl->AddRect(b0, b1, bd, m.radiusS, 0, 1.f);

    // Primary 顶部高光
    if (kind == BtnKind::Primary) {
        dl->AddRectFilled(ImVec2(b0.x, b0.y), ImVec2(b1.x, b0.y + h * 0.4f),
                          WithAlpha(IM_COL32(255, 255, 255, 255), 0.10f + a * 0.08f),
                          m.radiusS);
    }

    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(b0.x + (w - ts.x) * 0.5f, b0.y + (h - ts.y) * 0.5f), fg, label);

    ImGui::PopID();
    return clk;
}

bool Pill(const char* label, BtnKind kind, float width) {
    const Metrics& m = M();
    const Palette& p = P();

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = (width > 0.f) ? width : ImGui::GetContentRegionAvail().x;
    const float h = m.rowH * 1.12f;
    const float r = h * 0.5f;

    ImGui::InvisibleButton("##pill", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    bool clk = ImGui::IsItemClicked(0);

    float a = Anim(label, hov || held, 16.f);
    if (held) a = 1.f;

    ImU32 bg, fg;
    switch (kind) {
    case BtnKind::Primary:
        bg = Mix(p.accentDim, p.accent, 0.35f + a * 0.65f);
        fg = p.textOnAccent;
        break;
    case BtnKind::Danger:
        bg = Mix(IM_COL32(110, 26, 28, 220), IM_COL32(190, 42, 46, 245), a);
        fg = IM_COL32(255, 232, 232, 255);
        break;
    default:
        bg = Mix(p.bgItem, p.bgItemHover, a);
        fg = p.textHi;
        break;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b0 = cp, b1 = ImVec2(cp.x + w, cp.y + h);
    if (held) { b0.y += 1.f; b1.y += 1.f; }

    // 外光晕 (按下时更亮)
    if (a > 0.02f) {
        dl->AddRectFilled(ImVec2(b0.x - 2.f, b0.y - 2.f), ImVec2(b1.x + 2.f, b1.y + 2.f),
                          WithAlpha(kind == BtnKind::Danger ? p.danger : p.accent, 0.20f * a),
                          r + 2.f);
    }
    dl->AddRectFilled(b0, b1, bg, r);
    dl->AddRect(b0, b1,
                WithAlpha(kind == BtnKind::Danger ? p.danger : p.accent, 0.55f), r, 0, 1.2f);
    dl->AddRectFilled(ImVec2(b0.x, b0.y), ImVec2(b1.x, b0.y + h * 0.42f),
                      IM_COL32(255, 255, 255, 22), r);

    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(b0.x + (w - ts.x) * 0.5f, b0.y + (h - ts.y) * 0.5f), fg, label);

    ImGui::PopID();
    return clk;
}

// ============================================================
// 色点
// ============================================================
bool ColorDots(const char* id, int* idx, const ImU32* colors, int count, float dotR) {
    const Metrics& m = M();
    if (!idx || !colors || count <= 0) return false;
    if (dotR <= 0.f) dotR = 11.f * m.s;

    ImGui::PushID(id);
    const float step = dotR * 2.f + m.gap * 0.9f;
    float totalW = step * count - m.gap * 0.9f;
    float avail  = ImGui::GetContentRegionAvail().x;

    ImVec2 cp = ImGui::GetCursorScreenPos();
    // 右对齐
    float startX = cp.x + (avail - totalW) * 0.5f;
    if (startX < cp.x) startX = cp.x;

    bool changed = false;
    for (int i = 0; i < count; ++i) {
        char bid[32];
        snprintf(bid, sizeof(bid), "##dot%d", i);
        ImGui::SetCursorScreenPos(ImVec2(startX + i * step, cp.y));
        ImGui::InvisibleButton(bid, ImVec2(dotR * 2.f, dotR * 2.f));
        bool hov = ImGui::IsItemHovered();
        bool clk = ImGui::IsItemClicked(0);
        if (clk && *idx != i) { *idx = i; changed = true; }

        ImVec2 c(startX + i * step + dotR, cp.y + dotR);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(c, dotR, colors[i], 24);
        if (*idx == i) {
            dl->AddCircle(c, dotR + 3.f * m.s, IM_COL32(255, 255, 255, 235), 24, 2.f * m.s);
        } else if (hov) {
            dl->AddCircle(c, dotR + 2.f * m.s, WithAlpha(IM_COL32(255, 255, 255, 255), 0.5f),
                          24, 1.4f * m.s);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + dotR * 2.f));
    ImGui::PopID();
    return changed;
}

// ============================================================
// 折叠
// ============================================================
bool Collapse(const char* label, bool* open) {
    const Metrics& m = M();
    const Palette& p = P();
    const float rh = m.rowH * 0.92f;

    ImGui::PushID(label);
    ImVec2 cp = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    ImGui::InvisibleButton("##col", ImVec2(w, rh));
    bool hov = ImGui::IsItemHovered();
    bool clk = ImGui::IsItemClicked(0);
    if (clk && open) *open = !*open;

    float a = Anim(open, open ? *open : false, 13.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) {
        dl->AddRectFilled(cp, ImVec2(cp.x + w, cp.y + rh),
                          WithAlpha(p.bgItemHover, 0.5f), m.radiusS);
    }
    dl->AddText(ImVec2(cp.x + m.gap, cp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f),
                p.textHi, label);

    // 箭头: 0 向右, 1 向下
    float ax = cp.x + w - m.gap * 1.3f;
    float ay = cp.y + rh * 0.5f;
    float s  = 4.5f * m.s;
    float ang = a * 1.5708f;
    float c = cosf(ang), sn = sinf(ang);
    // 两个方向向量旋转
    ImVec2 d0(-s * 0.5f, -s), d1(s * 0.9f, 0.f), d2(-s * 0.5f, s);
    auto rot = [&](ImVec2 v) {
        return ImVec2(ax + v.x * c - v.y * sn, ay + v.x * sn + v.y * c);
    };
    dl->AddLine(rot(d0), rot(d1), p.textMid, 2.f * m.s);
    dl->AddLine(rot(d1), rot(d2), p.textMid, 2.f * m.s);

    ImGui::PopID();
    return clk && open;
}

// ============================================================
// Toast
// ============================================================
namespace {
struct ToastEntry {
    char msg[128];
    bool warn;
    float born;
    bool used;
};
constexpr int kToastMax = 4;
ToastEntry g_toasts[kToastMax];
int g_toastCursor = 0;

constexpr float kToastLife   = 2.4f;
constexpr float kToastFadeIn = 0.16f;
constexpr float kToastFadeOut= 0.34f;
} // namespace

void Toast(const char* msg, bool warn) {
    if (!msg) return;
    ToastEntry& e = g_toasts[g_toastCursor];
    snprintf(e.msg, sizeof(e.msg), "%s", msg);
    e.warn = warn;
    e.born = ImGui::GetTime();
    e.used = true;
    g_toastCursor = (g_toastCursor + 1) % kToastMax;
}

void DrawToasts() {
    const Metrics& m = M();
    const Palette& p = P();
    const float now = ImGui::GetTime();
    // 不要用 io.DisplaySize：它只在 Init_Render 与 NewFrame(true) 时更新，
    // NewFrame() 默认 resize=false，旋转后会是旧值。
    const ImVec2 disp = ImGui::GetMainViewport()->Size;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float stackY = disp.y - 76.f * m.s;

    for (int i = 0; i < kToastMax; ++i) {
        ToastEntry& e = g_toasts[i];
        if (!e.used) continue;

        float age = now - e.born;
        if (age < 0.f || age > kToastLife) { e.used = false; continue; }

        float alpha = 1.f;
        if (age < kToastFadeIn)            alpha = age / kToastFadeIn;
        else if (age > kToastLife - kToastFadeOut)
            alpha = (kToastLife - age) / kToastFadeOut;
        alpha = ClampF(alpha, 0.f, 1.f);

        ImVec2 ts = ImGui::CalcTextSize(e.msg);
        const float pw = ts.x + 34.f * m.s;
        const float ph = 36.f * m.s;
        ImVec2 a(disp.x * 0.5f - pw * 0.5f, stackY);
        ImVec2 b(a.x + pw, a.y + ph);

        ImU32 base = e.warn ? p.danger : p.accent;
        dl->AddRectFilled(ImVec2(a.x, a.y + 2.f), ImVec2(b.x, b.y + 2.f),
                          WithAlpha(p.shadow, 0.5f * alpha), ph * 0.5f);
        dl->AddRectFilled(a, b, WithAlpha(Mix(p.bgCardHead, base, 0.20f), 0.97f * alpha),
                          ph * 0.5f);
        dl->AddRect(a, b, WithAlpha(base, 0.7f * alpha), ph * 0.5f, 0, 1.f);
        dl->AddText(ImVec2(a.x + (pw - ts.x) * 0.5f, a.y + (ph - ts.y) * 0.5f),
                    WithAlpha(e.warn ? IM_COL32(255, 235, 235, 255) : p.textHi, alpha),
                    e.msg);

        stackY -= ph + 8.f * m.s;
    }
}

// ============================================================
// 明暗切换全屏过渡
//
// 设计: 两段式
//   1) 起手 0.10s: 遮罩从 0 冲到峰值 0.72 —— 这一段要快,
//      它负责"盖住"调色板跳变的那一帧。
//   2) 收尾 0.34s: 从峰值缓降到 0 —— 这一段要慢, 用 ease-out,
//      视觉上像光晕散开, 而不是"闪一下白屏又消失"。
//
// 颜色: 暗->亮时遮罩用白色(模拟被光洗过), 亮->暗时用黑色(模拟落影)。
// 这样两个方向的过渡在语义上也是对的, 不是单纯一层灰。
// ============================================================
namespace themeTransition {

static bool  g_active   = false;
static bool  g_toDark   = true;
static float g_t        = 0.f;      // 0 -> 1 总进度
static constexpr float kRise = 0.10f;
static constexpr float kFall = 0.34f;
static constexpr float kPeak = 0.72f;

void Start(bool toDark) {
    g_active = true;
    g_toDark = toDark;
    g_t      = 0.f;
}

bool Active() { return g_active; }

// ============================================================
// 遮罩绘制
//
// 【为什么必须传矩形, 不能用全屏】
//   前一版这里写的是
//       const ImVec2 disp = ImGui::GetMainViewport()->Size;
//       dl->AddRectFilled(ImVec2(0,0), disp, col);
//   结果是整个屏幕闪一下 —— 游戏画面、状态栏、桌面都被盖了一层
//   黑/白蒙版。用户看到的不是"面板换了配色", 而是"手机闪了一下"。
//
//   明暗切换是悬浮窗自身的属性, 过渡遮罩就该只在悬浮窗的范围内。
//   所以改成由调用方传入窗口矩形 (wp, ws), 按圆角裁剪后填充。
//
// 【为什么要 PushClipRect】
//   AddRectFilled 带圆角参数只影响四角, 矩形本身还是会超出窗口
//   一点点被画出去 (阴影区)。用 clip rect 把整个绘制裁进窗口内,
//   保证无论圆角多少都不会溢出到屏幕其它地方。
// ============================================================
void Draw(const ImVec2& wp, const ImVec2& ws, float rounding) {
    if (!g_active) return;

    const float dt = SafeDt();
    g_t += dt / (kRise + kFall);
    if (g_t >= 1.f) { g_t = 1.f; g_active = false; }

    // 换算成 0..1 的实际不透明度
    const float riseT = kRise / (kRise + kFall);
    float a;
    if (g_t < riseT) {
        // 起手: 线性冲到峰值 (够快, 才能盖住 SetDark 那一帧的跳变)
        a = (g_t / riseT) * kPeak;
    } else {
        // 收尾: ease-out 三次方降下来
        const float k = (g_t - riseT) / (1.f - riseT);   // 0..1
        const float e = 1.f - k;
        a = kPeak * e * e * e;
    }
    if (a <= 0.002f) return;

    if (ws.x <= 1.f || ws.y <= 1.f) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 col = g_toDark ? IM_COL32(0, 0, 0, (int)(a * 255.f))
                               : IM_COL32(255, 255, 255, (int)(a * 255.f));

    dl->PushClipRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), true);
    dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), col,
                      rounding > 0.f ? rounding : 0.f);
    dl->PopClipRect();
}

// 无参重载: 面板不可见时不画 (明暗过渡属于面板, 面板关掉就没有可过渡的
// 表面了)。保留这个签名是为了调用点不必区分"面板在不在"。
void Draw() {}

} // namespace themeTransition

// ============================================================
// 设置行 (标签左, 控件右)
//
// 参考工程 查理王者/CaesarUI 的设置页排布: 单列纵向,
// 每行一个控件, 标签左对齐、控件右对齐, 行间细分隔线。
// 触摸目标高度统一 m.rowH, 不易误触。
// ============================================================

bool RowToggle(const char* label, bool* v, const char* hint) {
    const Metrics& m = M();
    const Palette& p = P();
    const float tw = 46.f * m.s;
    const float th = 26.f * m.s;
    const float rh = m.rowH;

    ImGui::PushID(label);
    ImVec2 cp   = ImGui::GetCursorScreenPos();
    float  rowW = ImGui::GetContentRegionAvail().x;

    // 整行可点 (含标签区), 触摸面积最大化
    ImGui::InvisibleButton("##row", ImVec2(rowW, rh));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(0);
    if (clicked && v) *v = !*v;

    const float a = Anim(v, v ? *v : false, 14.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (hovered) {
        dl->AddRectFilled(cp, ImVec2(cp.x + rowW, cp.y + rh),
                          WithAlpha(p.bgItemHover, 0.50f), m.radiusS);
    }

    // 【提示文字已统一移除】
    // 原来这里在标签下方画一行 textLow 的灰色说明 (hint)。
    // 实际问题: 这些说明文字对比度低 (textLow * 0.92), 在游戏画面上
    // 基本看不清, 却又把行高撑成两行, 让界面显得拥挤。
    // 现在统一不画: 参数保留 (调用点不用改), 实现忽略。
    (void)hint;

    const float lineH = ImGui::GetTextLineHeight();
    const float labelTop = cp.y + (rh - lineH) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, labelTop));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4((v && *v) ? p.textHi : p.textMid));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // 开关轨道 (右侧)
    DrawSwitch(dl, ImVec2(cp.x + rowW - tw, cp.y + (rh - th) * 0.5f), tw, th, a,
               p.accent, p.track);

    // 行分隔线
    dl->AddRectFilled(ImVec2(cp.x, cp.y + rh - 1.f), ImVec2(cp.x + rowW, cp.y + rh),
                      WithAlpha(p.divider, 0.45f));

    ImGui::PopID();
    return clicked && v;
}

// ============================================================
// RowInfo —— 只读数值行 (标签左, 数值右)
// 用于性能面板: 标签 + 数值 + 可选说明, 不响应点击。
// 布局对齐 RowSlider 的 "标签左上 / 数值右上" 视觉。
// ============================================================
void RowInfo(const char* label, const char* value, const char* hint,
             ImU32 valueColor) {
    const Metrics& m = M();
    const Palette& p = P();
    const float lineH = ImGui::GetTextLineHeight();

    ImGui::PushID(label);
    const ImVec2 cp   = ImGui::GetCursorScreenPos();
    const float  rowW = ImGui::GetContentRegionAvail().x;

    // 行高: 有说明两行, 无说明一行
    // 【提示文字已统一移除】行高恒为单行, hint 参数保留但忽略
    (void)hint;
    const float rh = lineH + m.gapS * 1.2f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 标签
    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, cp.y + m.gapS * 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textMid));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // 数值 (右侧)
    if (value && value[0]) {
        const ImVec2 vs = ImGui::CalcTextSize(value);
        ImGui::SetCursorScreenPos(
            ImVec2(cp.x + rowW - vs.x - m.gapS, cp.y + m.gapS * 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ToVec4(valueColor ? valueColor : p.textHi));
        ImGui::TextUnformatted(value);
        ImGui::PopStyleColor();
    }

    // 行分隔线
    dl->AddRectFilled(ImVec2(cp.x, cp.y + rh - 1.f),
                      ImVec2(cp.x + rowW, cp.y + rh),
                      WithAlpha(p.divider, 0.45f));

    ImGui::PopID();
}

bool RowSlider(const char* label, float* v, float lo, float hi,
               const char* fmt, const char* hint) {
    const Metrics& m = M();
    const Palette& p = P();
    const float rh  = m.rowH * 1.62f;   // 比普通行高, 容纳标签+轨道
    const float lineH = ImGui::GetTextLineHeight();

    ImGui::PushID(label);
    ImVec2 cp   = ImGui::GetCursorScreenPos();
    float  rowW = ImGui::GetContentRegionAvail().x;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- 标签 (左上) ----
    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, cp.y + m.gapS * 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // ---- 数值 (右上) ----
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, v ? *v : 0.f);
    ImVec2 ns = ImGui::CalcTextSize(buf);
    ImGui::SetCursorScreenPos(ImVec2(cp.x + rowW - ns.x - m.gapS, cp.y + m.gapS * 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.accent));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();

    // ---- 轨道位置 ----
    // 【提示文字已统一移除】原来这里会在标签下方画一行 hint 并把
    // 轨道往下推一个行高; 现在固定紧贴标签, hint 参数保留但忽略。
    (void)hint;
    float trackTop = cp.y + m.gapS * 0.45f + lineH + m.gapS * 0.5f;

    // ---- 轨道 ----
    const float trackH = m.trackH;
    const float trackW = rowW - m.gapS * 2.f;
    ImVec2 t0(cp.x + m.gapS, trackTop);
    ImVec2 t1(t0.x + trackW, t0.y + trackH);

    // 命中区比轨道高, 便于触摸
    const float hitPad = m.gapS * 0.9f;
    ImGui::SetCursorScreenPos(ImVec2(t0.x, t0.y - hitPad));
    ImGui::InvisibleButton("##track", ImVec2(trackW, trackH + hitPad * 2.f));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    bool changed = false;
    float nv = v ? *v : 0.f;
    const float span = (hi > lo) ? (hi - lo) : 1.f;
    float t = ClampF((nv - lo) / span, 0.f, 1.f);

    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        float nt = ClampF((mx - t0.x) / (trackW > 0.f ? trackW : 1.f), 0.f, 1.f);
        float cand = lo + nt * span;
        if (cand != nv) { nv = cand; t = nt; changed = true; }
        if (v) *v = nv;
    }

    // 轨道底
    const float rd = trackH * 0.5f;
    dl->AddRectFilled(t0, t1, p.track, rd);
    dl->AddRect(t0, t1, WithAlpha(p.border, hovered ? 0.85f : 0.55f), rd, 0, 1.f);

    // 已填充
    float fx = t0.x + trackW * t;
    if (t > 0.001f) {
        dl->AddRectFilled(t0, ImVec2(fx, t1.y),
                          Mix(p.accentDim, p.accent, hovered ? 1.f : 0.85f), rd);
    }

    // 滑块
    float kr = m.knob * 0.5f;
    dl->AddCircleFilled(ImVec2(fx, (t0.y + t1.y) * 0.5f), kr,
                        hovered || active ? p.accent2 : p.accent, 20);
    dl->AddCircle(ImVec2(fx, (t0.y + t1.y) * 0.5f), kr,
                  WithAlpha(IM_COL32(255, 255, 255, 255), 0.5f), 20, 1.f);

    // 行分隔线
    const float rowEndY = cp.y + rh;
    dl->AddRectFilled(ImVec2(cp.x, rowEndY - 1.f), ImVec2(cp.x + rowW, rowEndY),
                      WithAlpha(p.divider, 0.45f));

    // 光标推进到行尾
    ImGui::SetCursorScreenPos(ImVec2(cp.x, rowEndY));

    ImGui::PopID();
    return changed;
}

bool RowSliderInt(const char* label, int* v, int lo, int hi, const char* hint) {
    float f = v ? (float)*v : 0.f;
    bool ch = RowSlider(label, &f, (float)lo, (float)hi, "%.0f", hint);
    if (ch && v) {
        int nv = (int)(f + 0.5f);
        if (nv < lo) nv = lo;
        if (nv > hi) nv = hi;
        *v = nv;
    }
    return ch;
}

bool RowCombo(const char* label, int* idx, const char* const items[], int count,
              const char* hint) {
    const Metrics& m = M();
    const Palette& p = P();

    // 【提示文字已统一移除】
    // 原来行高会被 hint 撑成两行(ImMax(m.rowH, lineH*2)), 并额外
    // 用 InvisibleButton 补占位。现在恒为单行 m.rowH, 不再有 hint 行,
    // 也不需要占位补丁 —— 少一个不可见按钮, 少一层布局耦合。
    (void)hint;
    const float rh = m.rowH;

    ImGui::PushID(label);
    ImVec2 cp   = ImGui::GetCursorScreenPos();
    float  rowW = ImGui::GetContentRegionAvail().x;

    // 标签垂直居中: 行高恒等于 m.rowH, 减去一行行高再折半即可
    const float labelTop = cp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 右侧下拉占位: 先算出宽度, 左侧标签的最大可用宽度才定得下来。
    // 原来标签不做宽度约束, 长标签("导航栏位置"/"框体样式")会一路画到
    // 下拉框底下, 表现为文字被遮挡。
    const float comboW = ImMin(rowW * 0.42f, 220.f * m.s);
    const float labelMaxW = rowW - comboW - m.gapS * 3.f;

    char lblBuf[256];
    Ellipsize(label, labelMaxW, lblBuf, (int)sizeof(lblBuf));

    ImGui::SetCursorScreenPos(ImVec2(cp.x + m.gapS, labelTop));
    ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(p.textHi));
    ImGui::TextUnformatted(lblBuf);
    ImGui::PopStyleColor();

    // 右侧下拉: 用现有 Combo 但宽度受限。
    // 垂直居中: rh 恒等于 m.rowH, 所以 (rh - comboH) / 2 为 0 ——
    // 保留这个式子而不是直接写 cp.y, 是为了将来行高变化时自动跟随。
    const float comboH = m.rowH;
    const float comboTop = cp.y + (rh - comboH) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(cp.x + rowW - comboW - m.gapS, comboTop));
    ImGui::PushItemWidth(comboW);
    // 【传空串, 不传 "##c"】
    //   历史写法 Combo("##c", ...) 是把 ImGui 的内部唯一化后缀当成 label 用。
    //   Combo 内部虽然补了剥离, 但只要有任何一条绘制路径没剥到
    //   (AddText / TextUnformatted / 未来新增的分支), "##c" 就会直接印在界面上。
    //
    //   这里改成空串: 空串在任何绘制路径下都渲染不出像素, 泄漏面归零。
    //   ID 唯一性不受影响 —— RowCombo 外层已经 PushID(真实标签) 包住了本行,
    //   Combo 内部的 PushID("") + GetID("##cb") 仍落在该行的 ID 栈里。
    bool changed = Combo("", idx, items, count);
    ImGui::PopItemWidth();

    dl->AddRectFilled(ImVec2(cp.x, cp.y + rh - 1.f), ImVec2(cp.x + rowW, cp.y + rh),
                      WithAlpha(p.divider, 0.45f));

    ImGui::PopID();
    return changed;
}

bool RowButton(const char* label, BtnKind kind, const char* hint) {
    const Metrics& m = M();
    ImGui::PushID(label);
    bool r = Button(label, kind, ImGui::GetContentRegionAvail().x);
    ImGui::PopID();
    return r;
}

} // namespace ui
} // namespace charlie
