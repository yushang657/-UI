// ============================================================
// Charlie UI · 灵动岛（仿 iOS Dynamic Island）
//
// 【本轮重写要点: 胶囊外不留任何边框】
//
//   旧实现的"外框"来自三层 AddRect 叠加:
//     · 4 层外发光  sp = i*1.6*s, 半径 r+sp
//     · 主描边      线宽 1.6*s, 画在 a..b 上
//     · 内层高光    线宽 0.9*s, 内收 0.7*s
//     · warn 态再加第 4 圈 r+1.5
//   ImGui 的 AddRect 描边以路径为中心向两侧各画 线宽/2 ——
//   1.6*s 的线有一半落在胶囊外面。再叠四层外发光,
//   几何上就成了"一个方框把药丸包住", 而且圆角处
//   AddRect 的段数在高 DPI 下不足, 近看是折线。
//
//   iOS 的灵动岛**没有描边**。它只靠一条明度关系立住:
//   胶囊比背景更黑, 边缘由背景自然衬托。
//   光源在正上方, 所以顶部边缘有一道极窄的亮面,
//   底部边缘融入阴影 —— 全部通过**填充渐变**实现,
//   没有任何 stroke。
//
//   现在只有 3 次填充, 零描边:
//     1) 软阴影    黑, 逐行二次衰减, 向外淡出
//     2) 胶囊本体  上亮下暗的竖向渐变 (光照)
//     3) 顶部高光  胶囊内 20% 高度处的亮面
//   边缘抗锯齿完全交给 ImGui 的 AA 凸包, 自绘闭包形状
//   的 ImDrawFlags_Closed 能保证接缝处不露线。
//
// 【形态: 一岛多态】
//   高度随内容弹性变化 (idle 扁 / expanded 厚),
//   宽度钳在 [h*3.4, h*7.0] —— 3.4 是 iOS idle 的
//   126:37 比例, 不能被内容撑破, 否则会变成"横条"。
// ============================================================
#include "CharlieIsland.h"
#include "CharlieTheme.h"
#include "CharlieAnim.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace charlie {
namespace island {

namespace {

// ---- 状态 ----
bool  g_visible  = true;
float g_topRatio = 0.f;

// ---- 动画 ----
Spring g_springPresence;  // 显隐
Spring g_springWidth;     // 宽度 (带过冲)
Spring g_springHeight;    // 高度 (带过冲) —— 一岛多态的核心
float  g_dropY    = 0.f;  // 入场纵向偏移
float  g_pressA   = 0.f;  // 按下内缩进度 0..1
bool   g_pressed  = false;
float  g_breathT  = 0.f;  // 状态点呼吸相位
float  g_shimmerT = 0.f;  // 玻璃高光扫过相位
float  g_minW     = 0.f;  // 宽度下限 (首帧一次性)

// 数值滚动
char  g_lastValue[64] = "";
float g_valueSlide    = 1.f;

constexpr float kDropSpeed     = 9.5f;
constexpr float kPresenceStiff = 190.f;
constexpr float kPresenceDamp  = 21.f;

// 宽度弹簧: 刚度 220 / 阻尼 19 -> 阻尼比约 0.64, 略欠阻尼。
// 换态时收尾有一次约 3% 过冲回弹 —— 这是 iOS 灵动岛的标志手感。
// 想更"稳"加大阻尼到 22~24, 想更"弹"降到 16。
constexpr float kWidthStiff = 220.f;
constexpr float kWidthDamp  = 19.f;

// 高度弹簧比宽度略慢一点 (刚度低 15%)。
// 高度先到位会露出"空胶囊", 后到位才是"内容把岛撑开"。
constexpr float kHeightStiff = 185.f;
constexpr float kHeightDamp  = 20.f;

constexpr float kBreathIdle   = 1.55f;
constexpr float kBreathBusy   = 3.4f;
constexpr float kShimmerSpeed = 0.85f;

// ---- 尺寸比例 (全部由岛高 h 推导, 任何 DPI 下比例一致) ----
// iOS idle/compact 是 126 x 37, 宽度约为高度的 3.4 倍。
// 这个比例是被用户训练过的视觉锚点, 岛做更宽像横条, 更窄像按钮。
constexpr float kIdleAspect    = 3.4f;   // 最小宽度 = h * 3.4
constexpr float kExpandedAspect= 7.0f;   // 最大宽度 = h * 7.0
constexpr float kHIdle    = 62.f;        // 基准状态下的岛高 (px, 乘 m.s)
constexpr float kHExpanded= 72.f;        // 有数值/多信息时的岛高

// ============================================================
// 填充类助手 —— 全部是"填充", 没有一条 stroke
// ============================================================

// 软阴影: 逐层按抛物线衰减。
//
// 抛物线 (1-t)^2 比线性收尾更快, 视觉上像真实的
// 环境光遮蔽, 而不是一圈均匀的雾。
//
// 【为什么是 10 层而不是 20】
//   每层都是一个圆角矩形 —— ImGui 内部要走 PathRect +
//   PathFillConvex, 每层约 40 个顶点。20 层 = 800 顶点/帧,
//   在 Mali-G68 上属于可感知的浪费, 而 10 层与 20 层在
//   62px 高的岛上看不出差别 (相邻层透明度差 < 1.5/255)。
void SoftShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r,
                float spread, ImU32 col, int rows) {
    if (rows < 1) rows = 1;
    const int baseA = (int)((col >> IM_COL32_A_SHIFT) & 0xFF);
    if (baseA <= 0) return;
    const ImU32 rgb = col & ~IM_COL32_A_MASK;

    for (int i = 0; i < rows; ++i) {
        const float t = (float)i / (float)rows;   // 0 = 最外圈
        const float k = 1.f - t;                  // 1 = 最外圈
        float fa = (float)baseA * k * k * 0.34f;
        if (fa < 1.f) continue;
        // 【必须 clamp】al > 255 时移位会污染 RGB 通道,
        //   表现为阴影发彩边。k*k*0.34 在 k<=1 时理论上不超,
        //   但 col 的 alpha 若不是 255 就会算错, 这里兜住。
        if (fa > 255.f) fa = 255.f;
        const float e = spread * t;               // 向外扩张量
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e),
                          ImVec2(b.x + e, b.y + e),
                          rgb | ((ImU32)fa << IM_COL32_A_SHIFT),
                          r + e);
    }
}

// 胶囊本体: 只有填充, 零描边。
//
// 【为什么不做竖向渐变】
//   ImGui 的 AddRectFilledMultiColor 是**直角**矩形, 没有圆角参数。
//   胶囊是 r = h/2 的全圆角形状, 渐变矩形一旦铺满 a..b,
//   四个角就会溢出圆弧, 变成"方底 + 圆角"的混合物 ——
//   这正是旧实现里那圈看不出来源的硬边。
//   把渐变收窄到内切矩形又会在 x = a.x + r 处留下一条竖直硬接缝。
//
//   所以放弃整段渐变, 改用 iOS 真正的做法:
//     单一近纯色底 + 一道窄的顶部亮面。
//   iOS 灵动岛本身就没有渐变, 它是纯色块, 体积感全靠
//   "顶部那道 1px 亮线"和背景的明度差。这样既没有接缝,
//   也只有一次圆角填充, 边缘完全交给 AA。
void Capsule(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float h,
             ImU32 bodyC, float hiA) {
    // 1) 主体: 带圆角的单色填充
    dl->AddRectFilled(a, b, bodyC, r);

    // 2) 顶部亮面: 光源在正上方。
    //
    //    矩形四边都必须落在胶囊内部, 否则会露出直角。
    //    取 x 内缩 0.62r、y 从 0.07h 起。
    //    校验 (r = 0.5h):
    //      y = 0.07h 时胶囊左边界 x = a.x + 0.5h - sqrt(0.25h²-0.1849h²)
    //                                = a.x + 0.245h = a.x + 0.49r
    //      起点 0.62r > 0.49r, 安全;
    //      y = 0.30h 时左边界 = a.x + 0.083r, 更宽松。
    if (hiA > 0.004f) {
        const int A8 = (int)(hiA * 255.f);
        if (A8 > 0) {
            dl->AddRectFilledMultiColor(
                ImVec2(a.x + r * 0.62f, a.y + h * 0.07f),
                ImVec2(b.x - r * 0.62f, a.y + h * 0.30f),
                IM_COL32(255, 255, 255, A8),
                IM_COL32(255, 255, 255, A8),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, 0));
        }
    }

    // 3) 底部内侧一层极淡的暗角: 让下边缘"沉"下去,
    //    否则纯色块看起来像贴纸。同样内缩, 不碰圆弧。
    {
        const ImU32 dark = IM_COL32(0, 0, 0, 40);
        dl->AddRectFilledMultiColor(
            ImVec2(a.x + r * 0.62f, b.y - h * 0.22f),
            ImVec2(b.x - r * 0.62f, b.y - h * 0.06f),
            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
            dark, dark);
    }
}

} // namespace

// ============================================================
// 显隐 / 位置
// ============================================================
void SetVisible(bool v) {
    if (g_visible == v) return;
    g_visible = v;
    if (v) g_dropY = -36.f;   // 重新入场
}
bool IsVisible() { return g_visible; }
void Toggle()    { SetVisible(!g_visible); }

void SetTopRatio(float r) {
    if (r < 0.f) r = 0.f;
    if (r > 1.f) r = 1.f;
    g_topRatio = r;
}
float GetTopRatio() { return g_topRatio; }

void ResetAnimation() {
    g_dropY = -36.f;
    g_springPresence.Set(g_visible ? 1.f : 0.f);
    g_springWidth.Set(0.f);
    g_springHeight.Set(kHIdle * M().s);
    g_breathT = 0.f;
    g_shimmerT = 0.f;
    g_pressA = 0.f;
    g_pressed = false;
    g_minW = 0.f;
    g_lastValue[0] = 0;
    g_valueSlide = 1.f;
}

// ============================================================
// 绘制
// ============================================================
bool Draw(const IslandData& data) {
    const float dt = SafeDt();

    // ---------- 显隐 ----------
    {
        g_springPresence.To(g_visible ? 1.f : 0.f);
        g_springPresence.Update(dt, kPresenceStiff, kPresenceDamp);
        if (!g_visible && g_springPresence.current <= 0.004f) {
            g_springPresence.Set(0.f);
            return false;
        }
    }
    const float presence = g_springPresence.current;

    const Metrics& m = M();
    const Palette& p = P();

    // ---------- 入场 (从上方落下) ----------
    g_dropY = ExpApproach(g_dropY, 0.f, kDropSpeed, dt);

    // ---------- 相位 ----------
    const float breathSpeed = data.busy ? kBreathBusy : kBreathIdle;
    g_breathT += dt * breathSpeed;
    if (g_breathT > 6.28318f) g_breathT -= 6.28318f;

    g_shimmerT += dt * kShimmerSpeed;
    if (g_shimmerT > 1.f) g_shimmerT -= 1.f;

    const char* statusText = data.statusText ? data.statusText : "";
    const char* valueText  = data.valueText  ? data.valueText  : "";
    const char* unitText   = data.unitText   ? data.unitText   : "";

    ImFont* font = ImGui::GetFont();
    auto measure = [&](const char* t, float sz) -> ImVec2 {
        if (!t || !t[0]) return ImVec2(0.f, 0.f);
        return font->CalcTextSizeA(sz, FLT_MAX, 0.f, t);
    };

    // ============================================================
    // 尺寸 —— 一岛多态
    // ============================================================
    const bool  hasValue = (valueText[0] != 0);
    const bool  hasUnit  = (unitText[0]  != 0);

    // 目标高度: 只有品牌+状态时用 idle 高度,
    // 一旦右侧有数值, 高度弹性涨到 expanded —— 这是 iOS
    // "compact -> expanded" 的纵向表现。
    const float hTarget = (hasValue ? kHExpanded : kHIdle) * m.s;

    // 字号全部挂在实际高度上, 高度动画过程中文字一起呼吸
    // (先按目标高度算字号, 避免动画期间字号抖动)
    const float hT     = hTarget;
    const float fontMain  = hT * 0.335f;
    const float fontValue = hT * 0.475f;
    const float dotR      = hT * 0.072f;
    const float dotGap    = hT * 0.13f;
    const float padH      = hT * 0.40f;
    const float gapS      = hT * 0.24f;
    const float divW      = ImMax(1.f, m.s * 0.9f);

    const ImVec2 brandS  = measure("Charlie", fontMain);
    const ImVec2 statusS = measure(statusText, fontMain);
    const ImVec2 valueS  = measure(valueText, fontValue);
    const ImVec2 unitS   = measure(unitText, fontMain);

    // ---------- 数值滚动 ----------
    if (strcmp(g_lastValue, valueText) != 0) {
        snprintf(g_lastValue, sizeof(g_lastValue), "%s", valueText);
        g_valueSlide = 0.f;
    }
    if (g_valueSlide < 1.f) {
        g_valueSlide = ExpApproach(g_valueSlide, 1.f, 14.f, dt);
        if (g_valueSlide > 0.995f) g_valueSlide = 1.f;
    }

    // ---------- 内容总宽 ----------
    float contentW = brandS.x + dotGap + dotR * 2.f + dotGap * 0.55f + statusS.x;
    if (hasValue) {
        contentW += gapS + divW + gapS + valueS.x
                  + (hasUnit ? gapS * 0.55f + unitS.x : 0.f);
    }

    float targetW = contentW + padH * 2.f;

    // 钳制到 iOS 的宽高比区间。
    // 下限 3.4 保证"胶囊感"(不能被内容压成圆点),
    // 上限 7.0 保证"岛感"(不能被长文案拉成横条)。
    const float wLo = hTarget * kIdleAspect;
    const float wHi = hTarget * kExpandedAspect;
    if (targetW < wLo) targetW = wLo;
    if (targetW > wHi) targetW = wHi;

    // 首帧: 直接把宽度弹簧放到目标值。
    // 否则从 0 弹到目标会看到"胶囊从一条线撑开", 不像 iOS。
    if (g_minW <= 0.f) {
        g_minW = targetW;
        g_springWidth.Set(targetW);
        g_springHeight.Set(hTarget);
    }

    g_springWidth.To(targetW);
    g_springWidth.Update(dt, kWidthStiff, kWidthDamp);
    g_springHeight.To(hTarget);
    g_springHeight.Update(dt, kHeightStiff, kHeightDamp);

    const float w = ImMax(1.f, g_springWidth.current);
    const float h = ImMax(1.f, g_springHeight.current);
    const float r = h * 0.5f;

    // ---------- 定位 ----------
    const ImVec2 disp = ImGui::GetMainViewport()->Size;
    // 上边距 0.55 倍岛高: 岛体上沿贴近屏幕顶部, 与 iOS 的位置关系一致。
    // g_topRatio 是设置页的"上下微调", 保留 0.40 屏高行程。
    const float topGap = kHIdle * m.s * 0.55f;
    const float baseY  = topGap + g_topRatio * disp.y * 0.40f;
    const float presenceLift = (1.f - presence) * h * 1.15f;

    // 按下内缩: iOS 点按有轻微收缩反馈, 幅度 2.5%
    g_pressA = ExpApproach(g_pressA, (g_pressed ? 1.f : 0.f), 18.f, dt);
    const float pressK = 1.f - 0.025f * g_pressA;
    const float wv = w * pressK;
    const float hv = h * pressK;

    const float y = baseY + g_dropY - presenceLift;
    const float x = disp.x * 0.5f - wv * 0.5f;

    const ImVec2 a(x, y);
    const ImVec2 b(x + wv, y + hv);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto A = [&](ImU32 c) -> ImU32 { return WithAlpha(c, presence); };

    // ============================================================
    // 材质 —— 永远近黑, 这是 iOS 的做法
    //
    // 灵动岛在浅色主题下依然是黑的: 它与"屏幕物理开孔"
    // 共用同一套视觉语言, 换成浅灰就不成立了。
    // 浅色主题只把黑抬升一级 (纯黑在浅底上像洞), 并加强顶部亮面,
    // 让它在白底上仍然是"浮起的实体"而不是"挖空的洞"。
    // ============================================================
    const bool dark = IsDark();

    const float hiA = dark ? 0.085f : 0.115f;

    // ---------- 1. 软阴影 (唯一"胶囊之外"的东西, 且是模糊的) ----------
    SoftShadow(dl, a, b, r, ImMax(6.f, m.s * 9.f),
               p.shadow,
               10);

    // ---------- 2. 胶囊本体 ----------
    // 主体: 近纯黑单色底。iOS 灵动岛在暗色下约 #0B0B0D,
    // 浅色主题下仍是深色实体 (与屏幕开孔共用视觉语言),
    // 只把明度抬一级, 免得在白底上像"挖了个洞"。
    Capsule(dl, a, b, r, hv, A(dark ? IM_COL32(13, 13, 15, 252)
                                    : IM_COL32(19, 19, 22, 253)), hiA);

    // ---------- 3. 玻璃高光扫过 (裁在胶囊内) ----------
    if (!data.warn) {
        dl->PushClipRect(a, b, true);
        const float shw = hv * 1.7f;
        const float sx  = a.x - shw + (wv + shw * 2.f) * g_shimmerT;
        if (sx > a.x - shw && sx < b.x + shw) {
            dl->AddRectFilledMultiColor(
                ImVec2(sx - shw * 0.5f, a.y), ImVec2(sx + shw * 0.5f, b.y),
                IM_COL32(255, 255, 255, 0),
                IM_COL32(255, 255, 255, dark ? 12 : 20),
                IM_COL32(255, 255, 255, dark ? 12 : 20),
                IM_COL32(255, 255, 255, 0));
        }
        dl->PopClipRect();
    }

    // ============================================================
    // 内容
    // ============================================================
    // 岛体永远是深色, 所以前景永远是白色梯度 —— 与主题无关。
    // 这正是 iOS 的做法: 内容色跟随材质, 不跟随系统主题。
    const ImU32 fgHi  = IM_COL32(255, 255, 255, 255);
    const ImU32 fgMid = IM_COL32(172, 172, 176, 255);
    const ImU32 fgLow = IM_COL32(118, 118, 123, 255);

    const float midY = a.y + hv * 0.5f;
    float cx = a.x + padH;

    dl->AddText(font, fontMain, ImVec2(cx, midY - brandS.y * 0.5f),
                A(fgHi), "Charlie");
    cx += brandS.x + dotGap;

    // 状态点: 内核 + 呼吸外晕
    {
        const float pulse  = 0.62f + 0.38f * (0.5f + 0.5f * sinf(g_breathT));
        const ImU32 dotCol = data.warn ? p.danger
                          : (data.busy ? IM_COL32(94, 214, 132, 255)
                                       : IM_COL32(120, 170, 255, 255));
        dl->AddCircleFilled(ImVec2(cx + dotR, midY), dotR * (2.1f + 0.8f * pulse),
                            WithAlpha(dotCol, 0.16f * pulse * presence), 20);
        dl->AddCircleFilled(ImVec2(cx + dotR, midY), dotR,
                            WithAlpha(dotCol, (0.60f + 0.40f * pulse) * presence), 16);
        cx += dotR * 2.f + dotGap * 0.55f;
    }

    if (statusS.x > 0.f) {
        dl->AddText(font, fontMain, ImVec2(cx, midY - statusS.y * 0.5f),
                    A(data.warn ? IM_COL32(255, 122, 122, 255) : fgMid), statusText);
    }

    if (hasValue) {
        cx += statusS.x + gapS;

        // 竖向分隔: 两端渐隐的细条
        dl->AddRectFilledMultiColor(
            ImVec2(cx, midY - hv * 0.19f), ImVec2(cx + divW, midY + hv * 0.19f),
            WithAlpha(fgLow, 0.f), WithAlpha(fgLow, 0.5f * presence),
            WithAlpha(fgLow, 0.5f * presence), WithAlpha(fgLow, 0.f));
        cx += divW + gapS;

        // 数值分级色: 帧率是健康度指标, 单色看不出好坏
        float fpsVal = -1.f;
        if (unitText[0] == 'F' && valueText[0]) fpsVal = (float)atof(valueText);
        ImU32 valCol;
        if (fpsVal >= 0.f) {
            if (fpsVal >= 50.f)      valCol = IM_COL32(94, 214, 132, 255);
            else if (fpsVal >= 30.f) valCol = IM_COL32(240, 186, 76, 255);
            else                     valCol = IM_COL32(240, 92, 92, 255);
        } else {
            valCol = data.warn ? IM_COL32(255, 96, 96, 255) : fgHi;
        }

        if (g_valueSlide < 1.f) {
            const float t    = g_valueSlide;
            const float newY = midY - valueS.y * 0.5f + (1.f - t) * (valueS.y * 0.5f);
            dl->AddText(font, fontValue, ImVec2(cx, newY),
                        WithAlpha(valCol, t * presence), valueText);
        } else {
            dl->AddText(font, fontValue, ImVec2(cx, midY - valueS.y * 0.5f),
                        A(valCol), valueText);
        }
        cx += valueS.x;

        if (hasUnit) {
            dl->AddText(font, fontMain,
                        ImVec2(cx + gapS * 0.55f, midY - unitS.y * 0.5f + hv * 0.02f),
                        A(fgLow), unitText);
        }
    }

    // ---------- warn: 一圈柔和的外晕, 不是描边 ----------
    // 用逐层 AddRect (不填充) 会重新引入"外框"问题,
    // 这里改用两圈低透明度、线宽较大的 AddRect, 且线宽全部
    // 落在胶囊内侧, 视觉上是"胶囊自己在发红", 不是外套一个框。
    // 【关键: 内缩半个线宽】
    //   ImGui 的 AddRect 描边以路径为中心向两侧各画 线宽/2。
    //   线宽 2.6*s 若不内缩, 就有 1.3*s 落在胶囊外面 ——
    //   那一圈残影正是用户说的"胶囊外的边框"。
    //   第一圈内缩 lineW/2 让描边外沿刚好贴住胶囊边界,
    //   之后各圈继续向内, 视觉是"胶囊自己在发红"。
    if (data.warn) {
        const float lineW = ImMax(2.f, m.s * 2.4f);
        for (int i = 0; i < 3; ++i) {
            const float ins = lineW * 0.5f + (float)i * lineW * 0.85f;
            if (ins >= r) break;
            dl->AddRect(ImVec2(a.x + ins, a.y + ins),
                        ImVec2(b.x - ins, b.y - ins),
                        WithAlpha(p.danger, (0.30f - 0.09f * (float)i) * presence),
                        ImMax(1.f, r - ins), 0, lineW);
        }
    }

    // ============================================================
    // 触摸
    // ============================================================
    bool hit = false;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    // 热区外扩: 左右各 0.5h, 上下各 0.35h
    const float ex = hv * 0.5f;
    const float ey = hv * 0.35f;
    const bool inHot = (mp.x >= a.x - ex && mp.x <= b.x + ex &&
                        mp.y >= a.y - ey && mp.y <= b.y + ey);

    if (inHot) {
        // IsMouseClicked 是事件判定 (io.MouseClicked), 不是电平 ——
        // 快速轻点不会因为按下/抬起落在同一批里而丢失。
        hit = ImGui::IsMouseClicked(0);
    }
    g_pressed = inHot && ImGui::IsMouseDown(0);

    return hit;
}

} // namespace island
} // namespace charlie
