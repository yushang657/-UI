// ============================================================
// Charlie UI · 自由帧率区块
//
// 屏幕左上角可拖动位置的 FPS 区块, 与灵动岛内的帧率二选一。
// 拖动总开关关闭后区块仍显示, 但完全不参与命中测试 —— 触摸穿透。
// ============================================================
#pragma once

namespace charlie {
namespace ui {
namespace freefps {

// 每帧调用。返回本帧是否吃掉了触摸 (拖动中或无拖动时命中)。
bool Draw();

// 当前配置下是否应该显示
bool Active();

// 是否正在拖动
bool Dragging();

// 屏幕旋转 / 上下文重建时清状态
void Reset();

} // namespace freefps
} // namespace ui
} // namespace charlie
