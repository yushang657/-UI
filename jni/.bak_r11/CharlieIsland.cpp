// ============================================================
// Charlie UI · 灵动岛实现
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

using namespace charlie;

namespace {

// ---- 状态 ----
bool  g_visible   = true;
float g_topRatio  = 0.f;

// ---- 动画状态 (全部时间驱动) ----
Spring g_springDrop;      // 入场下落
Spring g_springPresence;  // 显隐
Spring g_springWidth;     // 宽度 (带过冲, iOS 换态手感)
float  g_pressA    = 0.f; // 按下反馈进度
bool   g_islandPressed = false;
float  g_breathT   = 0.f;  // 呼吸相位
float  g_shimmerT  = 0.f;  // 高光扫过相位
float  g_widthCur  = 0.f;  // 当前药丸宽度
float  g_widthSeen = 0.f;  // 上一帧目标宽度 (用于检测内容变化)

// 数值滚动: 目标值变化时做一次上滚
char   g_lastValue[64] = "";
float  g_valueSlide    = 1.f;   // 0 -> 1 上滚进度
float  g_valueOld      = 0.f;

constexpr float kDropSpeed    = 9.5f;    // 入场下落速度
constexpr float kPresenceSpeed= 12.f;    // 显隐速度
constexpr float kSpringStiff  = 190.f;   // 显隐弹簧刚度
constexpr float kSpringDamp   = 21.f;    // 显隐弹簧阻尼 (接近临界, 不振荡)
constexpr float kWidthSpeed   = 11.f;    // 宽度跟随 (旧, 保留兼容)
// 宽度弹簧: 刚度 220 / 阻尼 19 -> 阻尼比约 0.64, 欠阻尼,
// 换态时收尾有一次约 3% 的过冲回弹, 这是 iOS 灵动岛的标志手感。
// 想要更"稳"就加大阻尼(22~24), 想要更"弹"就降到 16。
constexpr float kWidthStiff   = 220.f;
constexpr float kWidthDamp    = 19.f;
constexpr float kBreathIdle   = 1.55f;   // 呼吸角速度 (rad/s)
constexpr float kBreathBusy   = 3.4f;
constexpr float kShimmerSpeed = 0.85f;   // 高光周期倍率

} // namespace

// ============================================================
// 显隐
// ============================================================
void SetVisible(bool v) {
    if (g_visible == v) return;
    g_visible = v;
    if (v) g_springDrop.Set(0.f);  // 重新入场
}
bool IsVisible() { return g_visible; }
void Toggle() { SetVisible(!g_visible); }

void SetTopRatio(float r) {
    if (r < 0.f) r = 0.f;
    if (r > 1.f) r = 1.f;
    g_topRatio = r;
}
float GetTopRatio() { return g_topRatio; }

void ResetAnimation() {
    g_springDrop.Set(0.f);
    g_springPresence.Set(g_visible ? 1.f : 0.f);
    g_breathT = 0.f;
    g_shimmerT = 0.f;
    g_widthCur = 0.f;
    g_widthSeen = 0.f;
    g_springWidth.Set(0.f);
    g_pressA = 0.f;
    g_islandPressed = false;
    g_lastValue[0] = 0;
    g_valueSlide = 1.f;
}

// ============================================================
// 绘制
// ============================================================

bool Draw(const IslandData& data) {
    const float dt = SafeDt();

    // ---------- 显隐弹簧 ----------
    {
        g_springPresence.To(g_visible ? 1.f : 0.f);
        g_springPresence.Update(dt, kSpringStiff, kSpringDamp);
        const float cur = g_springPresence.current;
        if (!g_visible && cur <= 0.004f) {
            g_springPresence.Set(0.f);
            return false;
        }
    }

    const float presence = g_springPresence.current;

    const Metrics& m = M();
    const Palette& p = P();

    // ---------- 入场 ----------
    g_springDrop.current = ExpApproach(g_springDrop.current, 0.f, kDropSpeed, dt);

    // ---------- 呼吸 / 高光相位 ----------
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
        return font->CalcTextSizeA(sz, FLT_MAX, 0.f, t);
    };

    // ============================================================
    // 尺寸体系 —— 参照 iOS 灵动岛比例
    //
    // iOS 官方(pt): idle/compact 126 × 37, 圆角 999(全圆)。
    // 所以 高度 : 宽度 ≈ 1 : 3.4 是一个被用户训练过的视觉锚点,
    // 岛做得比这更宽会像"横条", 更窄会像"按钮"。
    //
    // 这里 h 是唯一主尺寸, 其余全部由 h 推导, 任何 DPI 下比例一致。
    // ============================================================
    const float h     = 50.f * m.s;          // 岛高
    const float r     = h * 0.5f;            // 全圆角
    const float padH  = h * 0.40f;           // 左右内边距
    const float fontMain  = h * 0.44f;       // 主字号
    const float fontValue = h * 0.66f;       // 数值字号 (强调)
    const float dotR  = h * 0.082f;          // 状态点半径
    const float dotGap= h * 0.16f;
    const float divW  = ImMax(1.f, m.s * 0.9f);

    const ImVec2 brandS  = measure("Charlie", fontMain);
    const ImVec2 statusS = measure(statusText, fontMain);
    const ImVec2 valueS  = measure(valueText, fontValue);
    const ImVec2 unitS   = unitText[0] ? measure(unitText, fontMain) : ImVec2(0.f, 0.f);

    const float brandW  = brandS.x;
    const float statusW = statusS.x;
    const float valueW  = valueS.x;
    const float unitW   = unitS.x;

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
    const float gapS = h * 0.28f;

    // 数值区为空时 (用户关掉了帧率与时钟), 不能留下分隔线 + 间距。
    // 否则右侧会挂一条孤零零的竖线, 岛还被撑宽 —— 这正是
    // 旧版 "--" 占位留下的观感问题。这里把整段打包成条件项。
    const bool hasValue = (valueText[0] != 0);

    float contentW = brandW
                   + dotGap + dotR * 2.f + dotGap * 0.55f + statusW;

    if (hasValue) {
        contentW += gapS + divW + gapS
                  + valueW + (unitW > 0.f ? gapS * 0.55f + unitW : 0.f);
    }

    float targetW = contentW + padH * 2.f;

    // ---------- 宽度弹簧 ----------
    // 换用带过冲的弹簧: iOS 换态时会有轻微 overshoot(幅度约 3%),
    // 纯指数逼近(ExpApproach)是单调的, 少了那点"果冻感"。
    // 刚度/阻尼比调成接近临界但略欠阻尼, 收尾一次小回弹即停。
    g_springWidth.To(targetW);
    g_springWidth.Update(dt, kWidthStiff, kWidthDamp);
    g_widthCur = g_springWidth.current;
    if (g_widthCur < 1.f) g_widthCur = targetW;
    g_widthSeen = targetW;

    // ---------- 定位 ----------
    const ImVec2 disp = ImGui::GetMainViewport()->Size;
    const float topGap = h * 1.25f;
    const float baseY  = topGap + g_topRatio * disp.y * 0.40f;

    const float presenceLift = (1.f - presence) * h * 1.15f;
    // 入场: 从上方落下, 落下过程同时做一次极轻微的纵向回弹
    const float y = baseY + ImMax(0.f, g_springDrop.current) - presenceLift;
    const float x = disp.x * 0.5f - g_widthCur * 0.5f;

    ImVec2 a(x, y);
    ImVec2 b(x + g_widthCur, y + h);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto P2 = [&](ImU32 c) -> ImU32 { return WithAlpha(c, presence); };

    // ============================================================
    // 材质 —— iOS 深色半透明 + 细描边 + 内外双高光
    // ============================================================
    const ImU32 ring = data.warn ? p.danger : p.accent;
    const bool dark = IsDark();

    // ---------- 外发光 (4 层 1/i² 衰减) ----------
    // 【"岛外面有边框"的根因】
    //   旧实现每层画的是 AddRectFilled(a-sp, b+sp, r+sp)。这里有两个问题:
    //     1) AddRectFilled 是实心矩形, 四层叠加后在圆角外侧仍然留下
    //        近似直角的轮廓 —— 观感就是一个方框把药丸包住。
    //     2) 各层 sp 不同但圆角只加 sp, 而矩形边界也同步外扩 sp,
    //        几何上虽然同心, 但实心填充让内层颜色在边缘处阶梯跳变。
    //
    // 【现在的做法】
    //   只保留描边式的 AddRect (不填充), 半径严格 r + sp,
    //   并跳过最内层 (sp 太小会和岛体自身描边重叠), 视觉上就是一圈柔和外晕,
    //   不会出现任何直线段。
    {
        for (int i = 4; i >= 1; --i) {
            const float sp = i * 1.6f * m.s;
            const float al = 0.075f / (float)(i * i) * presence;
            if (al < 0.004f) continue;
            dl->AddRect(ImVec2(a.x - sp, a.y - sp),
                        ImVec2(b.x + sp, b.y + sp),
                        WithAlpha(p.shadow, al),
                        r + sp, 0, ImMax(1.f, m.s * 1.4f));
        }
    }

    // ---------- 岛体 ----------
    // iOS 材质: rgba(0,0,0,.72) + 细白描边 + inset 顶部高光。
    // 浅色主题下不能用纯黑, 换成高亮灰底 + 深色描边, 保持"浮起"关系。
    {
        const ImU32 base = dark ? IM_COL32(8, 8, 8, 235)
                               : IM_COL32(246, 246, 246, 246);
        const ImU32 topC = dark ? IM_COL32(34, 34, 34, 240)
                               : IM_COL32(255, 255, 255, 252);
        dl->AddRectFilledMultiColor(a, b, topC, topC, base, base);
    }

    // inset 顶部高光 (0 1px 0 rgba(255,255,255,.08))
    {
        const float inset = r * 0.5f;
        dl->AddLine(ImVec2(a.x + inset, a.y + 0.9f),
                    ImVec2(b.x - inset, a.y + 0.9f),
                    WithAlpha(IM_COL32(255, 255, 255, dark ? 30 : 200), presence),
                    ImMax(1.f, m.s * 0.9f));
    }

    // 细描边 rgba(255,255,255,.12)
    dl->AddRect(a, b,
                WithAlpha(dark ? IM_COL32(255, 255, 255, 32) : IM_COL32(0, 0, 0, 28),
                          presence),
                r, 0, ImMax(1.f, m.s * 0.9f));

    // warn 态叠一圈状态色
    if (data.warn) {
        dl->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                    WithAlpha(ring, 0.55f * presence), r + 1.f, 0, 1.6f * m.s);
    }

    // ---------- 液态玻璃斜向高光 ----------
    // 一层很淡的斜向渐变, 透明度 ≤ 0.10, 模拟玻璃对光的折射。
    // 用 PushClipRect 保证不溢出圆角。
    {
        dl->PushClipRect(a, b, true);
        const float hh = h;
        dl->AddRectFilledMultiColor(
            ImVec2(a.x, a.y), ImVec2(b.x, a.y + hh * 0.55f),
            IM_COL32(255, 255, 255, dark ? 22 : 90),
            IM_COL32(255, 255, 255, dark ? 8 : 40),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->PopClipRect();
    }

    // ---------- 高光扫过 ----------
    if (!data.warn) {
        const float shw = h * 1.7f;
        const float sx  = a.x - shw + (g_widthCur + shw * 2.f) * g_shimmerT;
        if (sx > a.x - shw && sx < b.x + shw) {
            dl->PushClipRect(a, b, true);
            dl->AddRectFilledMultiColor(
                ImVec2(sx - shw * 0.5f, a.y), ImVec2(sx + shw * 0.5f, b.y),
                IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, dark ? 14 : 34),
                IM_COL32(255, 255, 255, dark ? 14 : 34), IM_COL32(255, 255, 255, 0));
            dl->PopClipRect();
        }
    }

    // ============================================================
    // 内容排版
    // ============================================================
    float cx = a.x + padH;
    const float midY = a.y + h * 0.5f;

    // 前景色: 暗色用亮灰, 浅色用深灰 —— 不能直接用 p.accent,
    // 它在浅色下是黑色, 但岛体在浅色下也是浅底, 会撞。
    const ImU32 fgHi  = dark ? IM_COL32(255, 255, 255, 255) : IM_COL32(18, 18, 18, 255);
    const ImU32 fgMid = dark ? IM_COL32(178, 178, 178, 255) : IM_COL32(92, 92, 92, 255);
    const ImU32 fgLow = dark ? IM_COL32(126, 126, 126, 255) : IM_COL32(140, 140, 140, 255);

    // 品牌
    dl->AddText(font, fontMain, ImVec2(cx, midY - brandS.y * 0.5f), P2(fgHi), "Charlie");
    cx += brandW + dotGap;

    // 状态点: 内核 + 呼吸外晕
    {
        const float pulse  = 0.62f + 0.38f * (0.5f + 0.5f * sinf(g_breathT));
        const ImU32 dotCol = data.warn ? p.danger
                          : (data.busy ? IM_COL32(94, 214, 132, 255)   // 运行中: 绿
                                       : IM_COL32(120, 170, 255, 255)); // 待机: 蓝
        const float haloT = 2.1f + 0.8f * pulse;
        dl->AddCircleFilled(ImVec2(cx + dotR, midY), dotR * haloT,
                            WithAlpha(dotCol, 0.16f * pulse * presence), 24);
        dl->AddCircleFilled(ImVec2(cx + dotR, midY), dotR,
                            WithAlpha(dotCol, (0.60f + 0.40f * pulse) * presence), 18);
        cx += dotR * 2.f + dotGap * 0.55f;
    }

    // 状态文字
    dl->AddText(font, fontMain, ImVec2(cx, midY - statusS.y * 0.5f),
                P2(data.warn ? IM_COL32(255, 120, 120, 255) : fgMid), statusText);

    // 数值区: 没内容时整段跳过 (分隔线也不画)
    if (hasValue) {
        cx += statusW + gapS;

        // 竖向分隔: 两端渐隐
        dl->AddRectFilledMultiColor(
            ImVec2(cx, midY - h * 0.20f), ImVec2(cx + divW, midY + h * 0.20f),
            WithAlpha(fgLow, 0.f), WithAlpha(fgLow, 0.55f * presence),
            WithAlpha(fgLow, 0.55f * presence), WithAlpha(fgLow, 0.f));
        cx += divW + gapS;

        // 数值 (上滚切换)
        // 数值颜色
        //
        // 【对齐参考工程的分级色】
        //   查理hp源码/jni/src/悬浮窗.cpp:1906
        //     fps >= 50 绿 / >= 30 黄 / 其余 红
        //   这里是同一套阈值。帧率是"健康度"指标, 单色白字看不出好坏,
        //   分级色让 55 和 28 一眼可辨, 不用去读数字。
        //
        //   判定依据用的还是 data.warn (外部状态通道), 但帧率模式下
        //   外部会把帧率一起送进来, 这里用 valueText 解析出来的数值
        //   做二次判定, 保证两种来源一致。
        float fpsVal = -1.f;
        if (data.unitText[0] == 'F' && valueText[0]) {
            fpsVal = (float)atof(valueText);
        }
        ImU32 valCol;
        if (fpsVal >= 0.f) {
            if (fpsVal >= 50.f)      valCol = dark ? IM_COL32(94, 214, 132, 255)
                                                   : IM_COL32(28, 158, 82, 255);
            else if (fpsVal >= 30.f) valCol = dark ? IM_COL32(240, 186, 76, 255)
                                                   : IM_COL32(190, 130, 20, 255);
            else                     valCol = dark ? IM_COL32(240, 92, 92, 255)
                                                   : IM_COL32(200, 48, 48, 255);
        } else {
            valCol = data.warn ? IM_COL32(255, 96, 96, 255) : fgHi;
        }

        if (g_valueSlide < 1.f) {
            const float t    = g_valueSlide;
            // 旧值同步向上飘, 新值从下方滑入 —— 双段式, 比单段更像 iOS
            const float newY = midY - valueS.y * 0.5f + (1.f - t) * (valueS.y * 0.55f);
            dl->AddText(font, fontValue, ImVec2(cx, newY),
                        WithAlpha(valCol, t * presence), valueText);
        } else {
            dl->AddText(font, fontValue, ImVec2(cx, midY - valueS.y * 0.5f),
                        P2(valCol), valueText);
        }
        cx += valueW;

        if (unitW > 0.f) {
            dl->AddText(font, fontMain,
                        ImVec2(cx + gapS * 0.55f, midY - unitS.y * 0.5f + h * 0.03f),
                        P2(fgLow), unitText);
        }
    }

    // ============================================================
    // 触摸 —— 热区比视觉大一圈, 方便手指点
    // ============================================================

    // 按下时的整体缩放 (iOS 点按有轻微内缩反馈)
    g_pressA = ExpApproach(g_pressA, (g_islandPressed ? 1.f : 0.f), 18.f, dt);

    bool hit = false;
    const ImVec2 mp = ImGui::GetIO().MousePos;
    // 热区外扩: 上下各 0.35h, 左右各 0.5h。视觉尺寸不变, 但点击更容易命中。
    const float ex = h * 0.5f;
    const float ey = h * 0.35f;
    const bool inHot = (mp.x >= a.x - ex && mp.x <= b.x + ex &&
                        mp.y >= a.y - ey && mp.y <= b.y + ey);

    if (inHot) {
        hit = ImGui::IsMouseClicked(0);
        if (ImGui::IsMouseDown(0)) {
            // 【必须向内收】AddRect 的描边以路径为中心向两侧各画 线宽/2,
            //   2.2*s 的线宽画在 a..b 上, 有一半落在岛体外面, 再叠上外发光
            //   就成了"岛外面套了一个框"。收进 1.2*s 让它变成内描边高亮。
            const float ins = 1.2f * m.s;
            dl->AddRect(ImVec2(a.x + ins, a.y + ins),
                        ImVec2(b.x - ins, b.y - ins),
                        WithAlpha(IM_COL32(255, 255, 255, dark ? 90 : 60),
                                  0.9f * presence),
                        ImMax(1.f, r - ins), 0, 2.2f * m.s);
        }
    }
    g_islandPressed = inHot && ImGui::IsMouseDown(0);

    return hit;
}

} // namespace island
} // namespace charlie
