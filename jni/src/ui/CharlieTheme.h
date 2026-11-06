// ============================================================
// Charlie UI · 主题层
// 品牌色板 + 屏幕比例度量 + ImGuiStyle 注入
// 作者: Charlie
// ============================================================
#pragma once

#include "imgui.h"
#include "imgui_internal.h"   // ImClamp / ImMax / ImMin

namespace charlie {

// ---------- 品牌色板 ----------
// 设计取向: 近黑冷底 + 薄荷青绿主强调 + 青色副强调
// 刻意与仓内既有 5 套 UI（冰蓝 / 紫晶 / 暗金 / 青绿 / iOS 蓝）区分开
struct Palette {
    ImU32 bgWin;        // 窗口底
    ImU32 bgCard;       // 卡片底
    ImU32 bgCardHead;   // 卡片标题条
    ImU32 bgContent;    // 内容区底
    ImU32 bgSide;       // 侧边栏底
    ImU32 bgItem;       // 控件底
    ImU32 bgItemHover;  // 控件悬停
    ImU32 bgItemActive; // 控件按下

    ImU32 accent;       // 主强调 (薄荷青绿)
    ImU32 accentDim;    // 主强调暗
    ImU32 accent2;      // 副强调 (青)
    ImU32 accentGlow;   // 主强调光晕 (低 alpha)
    ImU32 accentText;   // 强调色文字

    ImU32 textHi;       // 主文字
    ImU32 textMid;      // 次文字
    ImU32 textLow;      // 弱文字
    ImU32 textOnAccent; // 强调底上的文字

    ImU32 border;       // 描边
    ImU32 divider;      // 分割线
    ImU32 track;        // 滑轨 / 未选轨道

    ImU32 ok;
    ImU32 warn;
    ImU32 danger;
    ImU32 shadow;

    ImVec4 textHiV;
    ImVec4 textMidV;
    ImVec4 accentV;
    ImVec4 bgWinV;
    ImVec4 dangerV;
};

// ---------- 屏幕比例度量 ----------
// 所有尺寸都由 s 推导, 改屏幕/改用户缩放只动一个变量
struct Metrics {
    float s;        // 全局缩放系数 (基准 1080 短边 = 1.0)

    bool  landscape;  // 当前是否横屏
    float screenW;    // 当前屏幕逻辑宽
    float screenH;    // 当前屏幕逻辑高

    float fontXS;
    float fontS;
    float fontM;
    float fontL;
    float fontXL;

    float padX;
    float padY;
    float gap;
    float gapS;

    float rowH;
    float titleH;
    float sideW;
    float tabH;
    float cardPad;

    float radius;
    float radiusS;
    float radiusL;

    float knob;
    float trackH;

    float winW;
    float winH;
};

// 全局单例访问
Palette& P();

// ---------- 明暗切换 ----------
// SetDark 会重建整套调色板并重刷 ImGui 内建颜色。
// 调用后无需任何额外操作, 下一帧所有 P() 引用自动用新配色。
void SetDark(bool dark);
bool IsDark();
Metrics& M();

// 重新计算度量 (屏幕尺寸变化 / 用户缩放变化时调用)
void RebuildMetrics(float screenW, float screenH, float userScale);

// 把色板写入 ImGuiStyle (作为缩放基准, 每帧再乘 scale 覆盖)
void ApplyStyle();

// 便捷: ImU32 -> ImVec4
ImVec4 ToVec4(ImU32 c);
// 便捷: 改 alpha
ImU32 WithAlpha(ImU32 c, float a);
// 线性混色
ImU32 Mix(ImU32 a, ImU32 b, float t);

// 纯灰阶快速构造（R=G=B=v）。
// 明暗体系里大量需要"只在某处用一档灰度"的情况，
// 全部塞进 Palette 会膨胀，这里给一个就地构造器。
inline ImU32 grayc(int v, int a = 255) { return IM_COL32(v, v, v, a); }

} // namespace charlie
