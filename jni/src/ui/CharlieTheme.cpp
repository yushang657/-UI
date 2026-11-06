// ============================================================
// Charlie UI · 主题层实现
// ============================================================
#include "CharlieTheme.h"

#include <cmath>
#include <algorithm>

namespace charlie {

// ---------- 色板 ----------
// 纯黑白灰明暗体系。没有任何色相（R=G=B）。
//
// 设计规则：
//   - 层级只靠亮度差表达，从 bgSide(0) 到 textHi(255) 共 10 档
//   - "开启" = 亮（白），"关闭" = 暗（灰），两者明度差 > 120
//   - 状态色也不再带色相：ok=白、warn=灰白、danger=深灰
//     （需要区分时靠明度 + 图标形状，不靠颜色）
static Palette g_palette = {};
static bool    g_paletteReady = false;

// 前置声明: SetDark 要重刷 ImGui 内建颜色, 但 ApplyStyle 定义在后面
void ApplyStyle();

// 当前是否暗色。true = 原黑色阶梯(默认), false = 反相成浅色阶梯。
// 用 inverted 而不是重写一套 Palette: 所有引用 P() 的地方无需改动,
// 切主题只需重建一次调色板。
static bool    g_dark = true;

static inline ImU32 RGBA(int v, int a = 255) { return IM_COL32(v, v, v, a); }

// 明暗阶梯（唯一真实来源，其它地方全部引用这里）
//
// 【浅色模式怎么来的】
//   不是手写第二套常量, 而是把每档灰度取反: g' = 255 - g。
//   这样原来的"越暗越靠底"自动变成"越亮越靠底",
//   层级关系、对比度、文字与背景的明度差全部一一对应,
//   不会出现浅色下某个控件突然看不清的情况。
namespace gray {
    constexpr int c00 =   0;   // 纯黑
    constexpr int c04 =  10;   // 侧栏底
    constexpr int c08 =  20;   // 窗口底
    constexpr int c12 =  30;   // 内容区
    constexpr int c16 =  40;   // 卡片底
    constexpr int c20 =  52;   // 卡片头
    constexpr int c26 =  66;   // 控件底（关）
    constexpr int c32 =  82;   // 控件悬停
    constexpr int c40 = 102;   // 控件按下
    constexpr int c50 = 128;   // 中线 / 轨道
    constexpr int c60 = 154;   // 次级文字
    constexpr int c72 = 184;   // 描边亮
    constexpr int c85 = 217;   // 主文字
    constexpr int c93 = 236;   // 强调（亮态）
    constexpr int cFF = 255;   // 纯白（开启态）
}

// 取一档灰度, 自动按当前明暗模式反相
static inline int G(int v) { return g_dark ? v : (255 - v); }

static void BuildPalette() {
    Palette& p = g_palette;

    // ---------- 背景层级 ----------
    p.bgWin       = RGBA(G(gray::c08), 252);
    p.bgCard      = RGBA(G(gray::c16), 255);
    p.bgCardHead  = RGBA(G(gray::c20), 255);
    p.bgContent   = RGBA(G(gray::c12), 255);
    p.bgSide      = RGBA(G(gray::c04), 255);
    p.bgItem      = RGBA(G(gray::c26), 240);
    p.bgItemHover = RGBA(G(gray::c32), 250);
    p.bgItemActive= RGBA(G(gray::c40), 255);

    // ---------- 强调 ----------
    // 暗色: 白=开启。浅色: 黑=开启(反相后 cFF->0), 语义仍是"最强调"。
    p.accent      = RGBA(G(gray::cFF), 255);
    p.accentDim   = RGBA(G(gray::c60), 255);
    p.accent2     = RGBA(G(gray::c93), 255);
    p.accentGlow  = RGBA(G(gray::cFF), 40);
    p.accentText  = RGBA(G(gray::cFF), 255);

    // ---------- 文字 ----------
    p.textHi      = RGBA(G(gray::cFF), 255);
    p.textMid     = RGBA(G(gray::c60), 255);
    p.textLow     = RGBA(G(gray::c40), 255);
    p.textOnAccent= RGBA(G(gray::c00), 255);   // 强调底上用反相字

    // ---------- 线与轨 ----------
    p.border      = RGBA(G(gray::c32), 200);
    p.divider     = RGBA(G(gray::c26), 220);
    p.track       = RGBA(G(gray::c20), 255);

    // ---------- 状态 ----------
    p.ok          = RGBA(G(gray::cFF), 255);
    p.warn        = RGBA(G(gray::c85), 255);
    p.danger      = RGBA(G(gray::c50), 255);

    // 阴影: 暗色用纯黑; 浅色不能再用纯黑——浅底上的黑影会发脏,
    // 改用中灰 + 更低的透明度, 视觉上是一圈很淡的灰色晕开。
    p.shadow      = g_dark ? RGBA(gray::c00, 160) : RGBA(140, 90);

    // ImVec4 版本: 直接按反相后的灰度算, 不要再从 ImU32 里拆
    {
        const float hi = G(gray::cFF) / 255.f;
        const float md = G(gray::c60) / 255.f;
        const float bg = G(gray::c08) / 255.f;
        const float dg = G(gray::c50) / 255.f;
        p.textHiV   = ImVec4(hi, hi, hi, 1.f);
        p.textMidV  = ImVec4(md, md, md, 1.f);
        p.accentV   = ImVec4(hi, hi, hi, 1.f);
        p.bgWinV    = ImVec4(bg, bg, bg, 1.f);
        p.dangerV   = ImVec4(dg, dg, dg, 1.f);
    }

    g_paletteReady = true;
}

// ---------- 明暗切换 ----------
void SetDark(bool dark) {
    if (g_dark == dark) return;
    g_dark = dark;
    BuildPalette();   // 重建调色板, 所有 P() 引用自动生效
    ApplyStyle();     // ImGui 内建颜色也要跟着换
}

bool IsDark() { return g_dark; }

Palette& P() {
    if (!g_paletteReady) BuildPalette();
    return g_palette;
}

// ---------- 度量 ----------
static Metrics g_metrics = {};
static bool    g_metricsReady = false;

static inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void RebuildMetrics(float screenW, float screenH, float userScale) {
    if (screenW <= 0.f) screenW = 1920.f;
    if (screenH <= 0.f) screenH = 1080.f;

    const float shortSide = (screenW < screenH) ? screenW : screenH;
    const float longSide  = (screenW > screenH) ? screenW : screenH;
    const bool  landscape = (screenW > screenH);

    // 以 1080 短边为 1.0 基准
    float s = shortSide / 1080.f;
    // 超长条屏 (如 21:9) 视觉上偏瘦, 略微收一点
    if (longSide > shortSide * 1.95f) s *= 0.93f;
    s = ClampF(s, 0.80f, 1.55f);
    if (userScale > 0.f) s *= userScale;

    Metrics& m = g_metrics;
    m.s = s;
    m.landscape = landscape;
    m.screenW = screenW;
    m.screenH = screenH;

    m.fontXS = 12.f * s;
    m.fontS  = 14.f * s;
    m.fontM  = 17.f * s;
    m.fontL  = 21.f * s;
    m.fontXL = 28.f * s;

    m.padX    = 18.f * s;
    m.padY    = 14.f * s;
    m.gap     = 12.f * s;
    m.gapS    =  7.f * s;

    m.rowH    = 44.f * s;
    // 标题栏高度。
    // 【实测结论, 取代此前的猜测】
    //   在真机上打日志量到: win.size.y=65.2, radiusL=16.7, 按钮顶距窗口顶
    //   只有 12.2px。也就是按钮顶端落在窗口圆角的透明缺口里 —— 圆角半径
    //   16.7 > 留白 12.2, 上沿弧形直接把按钮顶部削掉一截。
    //
    //   所以标题栏高度必须满足: 按钮垂直留白 >= radiusL(18*s)。
    //   按钮是 44*s, 两侧各留 18*s -> 至少 80*s。取 84*s 留一点余量,
    //   再加用户要求的 2px 硬留白。
    //
    //   (上一版 68*s 只留 12*s, 仍小于 radiusL, 所以用户看到的"上面还是
    //    被遮挡了一点"确实存在, 不是错觉。)
    m.titleH  = 84.f * s + 2.f;
    // 横屏可用宽度大, 侧栏按比例放宽, 但封顶避免过宽
    m.sideW   = landscape ? ImMin(200.f * s, screenW * 0.20f) : 152.f * s;
    m.tabH    = 40.f * s;
    m.cardPad = 14.f * s;

    m.radius  = 12.f * s;
    m.radiusS =  8.f * s;
    m.radiusL = 18.f * s;

    m.knob    = 22.f * s;
    m.trackH  =  7.f * s;

    // ---------- 面板尺寸 ----------
    // 目标: 手机 / 平板 / 折叠屏 / 横竖屏都合适。
    //
    // 【竖屏关键约束】面板必须保持"横向铺开"的形态, 不能变成一根竖条。
    // 竖屏手机 1080x2400 时若按 hRatio=0.70 算, 高会到 1680 —— 面板被
    // 纵向拉很长, 视觉上像"向下拉了一大截", 很难看。
    //
    // 正确做法: 高度按【宽度】推导, 锁定一个舒适的宽高比区间,
    // 而不是按屏幕高度比例。这样竖屏下也是扁的横向面板。
    //
    //   目标宽高比: 横屏 1.55, 竖屏 1.30 (竖屏可用宽度小, 略方一点)
    //   兜底区间: [1.15, 1.85]
    // 【面板宽度】
    //   0.62 (1495px) 太宽, 0.50 (1206px) 又太窄 —— 取 0.57。
    //   在 2412x1080 上算出 1375px, 左右各留约 518px, 是"居中横向面板"的观感。
    //   竖屏 0.92 保持不动: 竖屏可用宽度本来就小, 再收会挤。
    float wRatio = landscape ? 0.57f : 0.92f;

    // 占屏比上限必须高于 wRatio, 否则上面的值会被这里再砍一刀。
    const float wCapRatio = landscape ? 0.63f : 0.94f;

    // 绝对尺寸下限(可交互), 用 s 缩放以适配小屏
    const float wMin = 300.f * s;
    const float hMin = 220.f * s;

    float w = screenW * wRatio;

    // 占屏比封顶
    const float wCap = screenW * wCapRatio;
    if (w > wCap) w = wCap;
    if (w < wMin) w = wMin;

    // 高度: 由宽度按目标宽高比推导, 保证面板始终横向
    //   1375 / 1.59 ≈ 865px —— 与下面 hScreenCap 的 864px 基本重合,
    //   两者取小后稳定落在 864px, 不会出现"改了 targetAspect 却没变化"。
    const float targetAspect = landscape ? 1.59f : 1.30f;
    float h = w / targetAspect;

    // 宽高比兜底: 极端屏幕下也保持横向观感
    const float minAspect = 1.15f;
    const float maxAspect = 1.85f;
    if (w / h < minAspect) h = w / minAspect;
    if (w / h > maxAspect) h = w / maxAspect;


    // 横屏时高度不能吃掉屏幕: 2400x1080 横屏若只按 1.55 算,
    // 高会到 960 (占屏 89%), 上下几乎贴边, 观感很挤。
    // 这里额外加一道"高度不超过屏幕 72%"的限制。
    if (landscape) {
        // 【这道上限曾经把高度死锁在 756px】
        //   0.70 * 1080 = 756。上一版 targetAspect 算出来的 818px 被它砍到 756,
        //   于是"调宽高比"完全无效 —— 用户看到的还是同一高度。
        //   现在放到 0.80 (864px), 给宽高比留出真正的生效空间。
        const float hScreenCap = screenH * 0.80f;
        if (h > hScreenCap) h = hScreenCap;
        // 高度被压后若宽度反而变窄, 只补到 minAspect, 不再反向放大宽度。
        // (旧代码用 w = h * minAspect 会把刚收窄的宽度又撑回去)
        if (w / h < minAspect) w = ImMin(h * minAspect, screenW * wCapRatio);
    }

    if (h < hMin) h = hMin;

    // 边距保护: 任何朝向都不许溢出屏幕
    const float marginX = 20.f * s;
    const float marginY = 20.f * s;
    if (w > screenW - marginX * 2.f) w = screenW - marginX * 2.f;
    if (h > screenH - marginY * 2.f) h = screenH - marginY * 2.f;

    // 高度被屏幕钳制后, 宽度可能显得过宽 -> 反过来按兜底比例收窄
    if (w / h > maxAspect) w = h * maxAspect;

    // 兜底: 极小屏上至少留出可交互尺寸
    if (w < 200.f) w = 200.f;
    if (h < 160.f) h = 160.f;

    m.winW = w;
    m.winH = h;

    g_metricsReady = true;
}

Metrics& M() {
    if (!g_metricsReady) RebuildMetrics(1920.f, 1080.f, 1.f);
    return g_metrics;
}

// ---------- 工具 ----------
ImVec4 ToVec4(ImU32 c) {
    return ImVec4(((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.f,
                  ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.f,
                  ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.f,
                  ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.f);
}

ImU32 WithAlpha(ImU32 c, float a) {
    if (a < 0.f) a = 0.f;
    if (a > 1.f) a = 1.f;
    return (c & 0x00FFFFFFu) | ((ImU32)(a * 255.f) << IM_COL32_A_SHIFT);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    if (t <= 0.f) return a;
    if (t >= 1.f) return b;
    const int ar = (a >> IM_COL32_R_SHIFT) & 0xFF, br = (b >> IM_COL32_R_SHIFT) & 0xFF;
    const int ag = (a >> IM_COL32_G_SHIFT) & 0xFF, bg = (b >> IM_COL32_G_SHIFT) & 0xFF;
    const int ab = (a >> IM_COL32_B_SHIFT) & 0xFF, bb = (b >> IM_COL32_B_SHIFT) & 0xFF;
    const int aa = (a >> IM_COL32_A_SHIFT) & 0xFF, ba = (b >> IM_COL32_A_SHIFT) & 0xFF;
    return IM_COL32((int)(ar + (br - ar) * t),
                    (int)(ag + (bg - ag) * t),
                    (int)(ab + (bb - ab) * t),
                    (int)(aa + (ba - aa) * t));
}

// ---------- 样式注入 ----------
// 注意: 这里写入的是「缩放基准」, 每帧由 shell 乘上 ui_scale 后覆盖
void ApplyStyle() {
    const Palette& p = P();
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    c[ImGuiCol_Text]                  = ToVec4(p.textHi);
    c[ImGuiCol_TextDisabled]          = ToVec4(p.textLow);
    c[ImGuiCol_WindowBg]              = ToVec4(p.bgWin);
    c[ImGuiCol_ChildBg]               = ToVec4(p.bgCard);
    c[ImGuiCol_PopupBg]               = ToVec4(p.bgCard);
    c[ImGuiCol_Border]                = ToVec4(p.border);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = ToVec4(p.bgItem);
    c[ImGuiCol_FrameBgHovered]        = ToVec4(p.bgItemHover);
    c[ImGuiCol_FrameBgActive]         = ToVec4(p.bgItemActive);
    c[ImGuiCol_TitleBg]               = ToVec4(p.bgSide);
    c[ImGuiCol_TitleBgActive]         = ToVec4(p.bgSide);
    c[ImGuiCol_TitleBgCollapsed]      = ToVec4(p.bgSide);
    c[ImGuiCol_MenuBarBg]             = ToVec4(p.bgSide);
    c[ImGuiCol_ScrollbarBg]           = ToVec4(p.bgContent);
    c[ImGuiCol_ScrollbarGrab]         = ToVec4(p.track);
    c[ImGuiCol_ScrollbarGrabHovered]  = ToVec4(p.accentDim);
    c[ImGuiCol_ScrollbarGrabActive]   = ToVec4(p.accent);
    c[ImGuiCol_CheckMark]             = ToVec4(p.accent);
    c[ImGuiCol_SliderGrab]            = ToVec4(p.accent);
    c[ImGuiCol_SliderGrabActive]      = ToVec4(p.accentText);
    c[ImGuiCol_Button]                = ToVec4(p.bgItem);
    c[ImGuiCol_ButtonHovered]         = ToVec4(p.bgItemHover);
    c[ImGuiCol_ButtonActive]          = ToVec4(p.accentDim);
    c[ImGuiCol_Header]                = ImVec4(p.accentV.x, p.accentV.y, p.accentV.z, 0.28f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(p.accentV.x, p.accentV.y, p.accentV.z, 0.42f);
    c[ImGuiCol_HeaderActive]          = ImVec4(p.accentV.x, p.accentV.y, p.accentV.z, 0.58f);
    c[ImGuiCol_Separator]             = ToVec4(p.divider);
    c[ImGuiCol_SeparatorHovered]      = ToVec4(p.accentDim);
    c[ImGuiCol_SeparatorActive]       = ToVec4(p.accent);
    c[ImGuiCol_ResizeGrip]            = ToVec4(p.track);
    c[ImGuiCol_ResizeGripHovered]     = ToVec4(p.accentDim);
    c[ImGuiCol_ResizeGripActive]      = ToVec4(p.accent);
    c[ImGuiCol_Tab]                   = ToVec4(p.bgItem);
    c[ImGuiCol_TabHovered]            = ToVec4(p.bgItemHover);
    c[ImGuiCol_TabActive]             = ImVec4(p.accentV.x, p.accentV.y, p.accentV.z, 0.42f);
    c[ImGuiCol_TabUnfocused]          = ToVec4(p.bgCard);
    c[ImGuiCol_TabUnfocusedActive]    = ToVec4(p.bgItem);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(p.accentV.x, p.accentV.y, p.accentV.z, 0.35f);
    c[ImGuiCol_NavHighlight]          = ToVec4(p.accent);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.f, 0.f, 0.f, 0.62f);

    // 圆角体系统一由 metrics 决定, 这里只给基准值
    const Metrics& m = M();
    s.WindowRounding    = m.radiusL;
    s.ChildRounding     = m.radius;
    s.FrameRounding     = m.radiusS;
    s.PopupRounding     = m.radius;
    s.GrabRounding      = m.radiusS;
    s.ScrollbarRounding = m.radiusS;
    s.TabRounding       = m.radiusS;

    s.WindowBorderSize = 0.f;
    s.ChildBorderSize  = 0.f;
    s.FrameBorderSize  = 0.f;
    s.PopupBorderSize  = 1.f;
    s.TabBorderSize    = 0.f;

    s.WindowPadding      = ImVec2(0.f, 0.f);
    s.FramePadding       = ImVec2(m.padX * 0.6f, m.padY * 0.45f);
    s.ItemSpacing        = ImVec2(m.gap * 0.7f, m.gap * 0.55f);
    s.ItemInnerSpacing   = ImVec2(m.gapS * 0.6f, m.gapS * 0.5f);
    s.IndentSpacing      = m.padX;
    s.ScrollbarSize      = 6.f * m.s;
    s.GrabMinSize        = m.knob;
    s.WindowMinSize      = ImVec2(120.f * m.s, 120.f * m.s);
    // 触摸场景下光标没有意义, 且能省一次命中测试
    s.MouseCursorScale   = 0.f;
}

} // namespace charlie
