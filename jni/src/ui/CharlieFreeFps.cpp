// ============================================================
// Charlie UI · 自由帧率区块
//
// 【这是什么】
//   把 FPS 数值单独甩到屏幕上, 供"不想被面板/灵动岛遮挡画面"的场景用。
//   形态极简: 只有数字本身。
//
// 【为什么只剩数字】
//   前一版是胶囊 + 数值 + 单位 + 分隔点 + 帧时 + 尖峰标记,
//   信息密度对调试够用, 但常态挂着时:
//     1) 底板、描边、阴影、左侧色条全是视觉噪音, 挡住游戏画面;
//     2) "60 FPS · 16.7ms" 这种组合读起来要扫视, 不如一个大数字直接;
//   所以现在只画数字。颜色仍然按帧率分级, 健康度靠颜色传达, 不靠文字。
//
// 【字号】
//   23*s -> 138*s (6 倍)。1080 短边下 s≈1.0, 即约 184px 高,
//   隔着一米看也能读出来。
//
// 【交互穿透怎么做到】
//   本工程的悬浮窗不是 Java WindowManager 视图, 而是
//   ANativeWindowCreator::Create() 走 SurfaceComposerClient 建的图层。
//   输入不经过 Android InputFlinger —— 我们自己去旁听 /dev/input/event*,
//   换算坐标后注入 ImGui。所以"这块区域是否吃掉触摸"完全由我们的
//   坐标判定决定:
//     fpsFreeDrag == true   命中数字包围盒 -> 认领触摸, 拖动位置
//     fpsFreeDrag == false  完全不参与命中测试 -> 触摸落到下层
//   不需要 setInputRegion / FLAG_NOT_TOUCHABLE —— 那些是 Java
//   WindowManager 层的概念, 在这个架构里没有对应物。
// ============================================================
#include "CharlieFreeFps.h"

#include "CharlieAnim.h"
#include "CharlieState.h"
#include "CharlieTheme.h"
#include "CharlieTouch.h"

#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace charlie {
namespace ui {
namespace freefps {

namespace {

bool  g_dragging = false;
float g_grabDX   = 0.f;
float g_grabDY   = 0.f;
float g_alpha    = 0.f;

// 拖动吸附阈值 (归一化)。松手后靠近边缘会吸到边缘留白处,
// 避免数字刚好卡在屏幕中间挡住准心。
constexpr float kSnap = 0.05f;

// 字号倍数。原来 23.f, 现在 x6。
constexpr float kFontMul = 6.f;
constexpr float kFontBase = 23.f;

float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// 帧率分级颜色 —— 抄参考工程 查理hp源码/悬浮窗.cpp:1906 的三档阈值
//   >= 50 绿 (ok)   >= 30 黄 (warn)   其余 红 (danger)
// 只剩数字之后, 颜色成了唯一的状态信号, 所以这三档必须留着。
ImU32 FpsColor(float fps, bool dark) {
    if (fps >= 50.f) return dark ? IM_COL32(94, 214, 132, 255) : IM_COL32(16, 140, 68, 255);
    if (fps >= 30.f) return dark ? IM_COL32(240, 186, 76, 255)  : IM_COL32(176, 116, 10, 255);
    return dark ? IM_COL32(240, 92, 92, 255)  : IM_COL32(190, 36, 36, 255);
}

} // namespace

// ============================================================
// 绘制
//
// 布局: 只有一个数字, 锚点是它的左上角。
// 拖动时整块跟着手指走, 松手贴边吸附。
// ============================================================
bool Draw() {
    const Features& f = F();
    const Metrics&  m = M();

    // 【关键】自由模式下灵动岛整个不画。
    // 岛的绘制在 CharlieShell.cpp 里判断, 这里只负责数值块。
    const bool want = (f.showFps && f.fpsPos == 1);

    const float dt = SafeDt();
    g_alpha = ExpApproach(g_alpha, want ? 1.f : 0.f, 12.f, dt);
    if (!want && g_alpha < 0.004f) {
        g_dragging = false;
        return false;
    }

    const ImVec2 disp = ImGui::GetMainViewport()->Size;

    ImFont* font = ImGui::GetFont();
    const float fs = kFontBase * kFontMul * m.s;

    const float fps = S().fps;
    char num[16];
    snprintf(num, sizeof(num), "%.0f", fps);

    // 【为什么用 CalcTextSizeA 而不是 CalcTextSize】
    //   本工程的全局字号是 31px, 而这里要画 138px。
    //   CalcTextSize 用的是当前 window 的 FontSize, 不是我们要的尺寸;
    //   CalcTextSizeA 显式传尺寸, 才能拿到真实包围盒 —— 拖动命中区和
    //   贴边吸附都要靠它算, 算错就会出现"看得见但点不到"。
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, num);

    // ---------- 位置 ----------
    float px = Clamp01(f.fpsFreeX) * disp.x;
    float py = Clamp01(f.fpsFreeY) * disp.y;

    const float maxX = ImMax(0.f, disp.x - sz.x);
    const float maxY = ImMax(0.f, disp.y - sz.y);
    px = ImMin(px, maxX);
    py = ImMin(py, maxY);

    ImVec2 a(px, py);
    ImVec2 b(px + sz.x, py + sz.y);

    // ---------- 交互 ----------
    // 命中区外扩一点: 纯文字没有底板, 严格贴包围盒的话手指很难按准,
    // 拖动会变成"戳好几次才抓住"。
    const float hotP = 10.f * m.s;
    const ImVec2 h0(a.x - hotP, a.y - hotP);
    const ImVec2 h1(b.x + hotP, b.y + hotP);

    bool hit = false;
    if (f.fpsFreeDrag) {
        float tx = 0.f, ty = 0.f;
        bool  tdown = false;
        if (charlie::touch::IsRunning()) {
            charlie::touch::GetPos(&tx, &ty);
            tdown = charlie::touch::IsDown();
        } else {
            const ImGuiIO& io = ImGui::GetIO();
            tx = io.MousePos.x;  ty = io.MousePos.y;  tdown = io.MouseDown[0];
        }

        const bool inBox = (tx >= h0.x && tx <= h1.x && ty >= h0.y && ty <= h1.y);

        static bool s_prevDown = false;
        const bool edge = (tdown && !s_prevDown);
        s_prevDown = tdown;

        Features& mf = const_cast<Features&>(f);

        if (!g_dragging && edge && inBox) {
            g_dragging = true;
            g_grabDX = tx - a.x;
            g_grabDY = ty - a.y;
            hit = true;
        }

        if (g_dragging) {
            if (tdown) {
                float nx = tx - g_grabDX;
                float ny = ty - g_grabDY;
                nx = nx < 0.f ? 0.f : (nx > maxX ? maxX : nx);
                ny = ny < 0.f ? 0.f : (ny > maxY ? maxY : ny);
                mf.fpsFreeX = (disp.x > 0.f) ? (nx / disp.x) : 0.f;
                mf.fpsFreeY = (disp.y > 0.f) ? (ny / disp.y) : 0.f;
                a = ImVec2(nx, ny);
                b = ImVec2(nx + sz.x, ny + sz.y);
                hit = true;
            } else {
                // 松手贴边: 靠近哪边吸到哪边
                if (mf.fpsFreeX < kSnap)             mf.fpsFreeX = 0.010f;
                else if (mf.fpsFreeX > 1.f - kSnap)  mf.fpsFreeX = 1.f - (sz.x / disp.x) - 0.010f;
                if (mf.fpsFreeY < kSnap)             mf.fpsFreeY = 0.008f;
                else if (mf.fpsFreeY > 1.f - kSnap)  mf.fpsFreeY = 1.f - (sz.y / disp.y) - 0.008f;
                g_dragging = false;
            }
        }
    } else {
        g_dragging = false;
    }

    // ---------- 绘制 ----------
    // 【为什么用前景 drawlist】没有窗口承载, 而且必须压在游戏画面之上。
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float al = g_alpha;
    const bool  dark = IsDark();
    const ImU32 col = FpsColor(fps, dark);

    // 文字描边: 没有底板, 数字会直接落在游戏画面上 ——
    // 浅色天空/白色烟雾这类背景下, 纯色数字会糊成一片。
    // 用 8 向偏移画一圈深色/浅色底, 等于给字加 1~2px 的对比轮廓,
    // 比加半透明底板更省画面: 只覆盖笔画外侧, 不覆盖一大块矩形。
    const float o = ImMax(1.5f, fs * 0.035f);
    const ImU32 halo = dark ? IM_COL32(0, 0, 0, (int)(190.f * al))
                            : IM_COL32(255, 255, 255, (int)(205.f * al));
    for (int i = 0; i < 8; ++i) {
        const float ang = (float)i * 0.7853982f;
        const float ox = cosf(ang) * o;
        const float oy = sinf(ang) * o;
        dl->AddText(font, fs, ImVec2(a.x + ox, a.y + oy), halo, num);
    }

    // 数字本体
    dl->AddText(font, fs, a, WithAlpha(col, al), num);

    // 拖动中给一点提示: 数字下方一条细横线, 表示"已抓住"。
    // 极简形态下不放任何框, 一条线足够, 而且不挡画面。
    if (g_dragging) {
        const float lw = sz.x;
        const float ly = b.y + 6.f * m.s;
        dl->AddRectFilled(ImVec2(a.x, ly), ImVec2(a.x + lw, ly + ImMax(1.5f, 3.f * m.s)),
                          WithAlpha(col, 0.75f * al), 1.5f * m.s);
    }

    return hit || g_dragging;
}

bool Active() {
    const Features& f = F();
    return f.showFps && f.fpsPos == 1;
}

bool Dragging() { return g_dragging; }

void Reset() {
    g_dragging = false;
    g_alpha = 0.f;
    g_grabDX = g_grabDY = 0.f;
}

} // namespace freefps
} // namespace ui
} // namespace charlie
