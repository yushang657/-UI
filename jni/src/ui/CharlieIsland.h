// ============================================================
// Charlie UI · 灵动岛（常驻信息条形态）
// 形态: 顶部居中药丸, 只承载「品牌 / 状态 / 数值」三要素,
//       不做点击展开 —— 触摸不改变形态, 只给视觉反馈。
// 动效: 入场下落 + 弹性宽度跟随内容 + 状态点呼吸 + 数值滚动
// ============================================================
#pragma once

#include "imgui.h"

namespace charlie {

// 灵动岛对外数据
struct IslandData {
    const char* statusText = "就绪";   // 左侧状态文字
    const char* valueText  = "--";     // 右侧数值 (人数 / FPS / 任意)
    const char* unitText   = "";       // 数值后缀, 如 "人"
    bool  warn  = false;               // 危险态: 状态点转红, 药丸描边变红
    bool  busy  = false;               // 忙碌态: 状态点加速呼吸
};

namespace island {

// 每帧调用一次, 绘制常驻信息条
// 返回: 是否被点击 (不改变形态, 仅作事件回调)
bool Draw(const IslandData& data);

// 显隐控制 (音量键 / 快捷键调用)
void SetVisible(bool v);
bool IsVisible();
void Toggle();

// 纵向位置 (0 = 屏幕顶部, 1 = 屏幕顶部 + 40% 屏高)
void SetTopRatio(float r);
float GetTopRatio();

// 重置全部动画状态 (窗口重开时调用, 让入场动画重播)
void ResetAnimation();

} // namespace island
} // namespace charlie
