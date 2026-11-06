// ============================================================
// Charlie UI · 触摸输入
// 悬浮层由 SurfaceComposer 创建, 不属于 Android 输入焦点体系,
// 收不到 AInputEvent。因此直接从 /dev/input 只读监听触摸屏,
// 换算成屏幕坐标后喂给 ImGui 的事件队列。
//
// 有意与仓内 TouchHelperA 不同:
//   1. 只读打开, 不做 EVIOCGRAB —— 不抢系统触摸
//   2. 不创建 uinput 虚拟设备 —— 不注入, 不需要 root 写权限
//   3. 走 io.AddMousePosEvent / AddMouseButtonEvent 事件队列,
//      不再直接改 io.MousePos / io.MouseDown (1.91 会互相覆盖)
//   4. 坐标系按 orientation 正确换算, 竖屏/横屏都对
// ============================================================
#pragma once

#include <functional>

namespace charlie {

namespace touch {

// 启动监听。
// screenW / screenH: 当前朝向下的屏幕逻辑尺寸 (用于坐标归一化)
// orientation:       displayInfo.orientation (0/1/2/3)
// 返回 false 表示没有可用的触摸设备
bool Start(int screenW, int screenH, int orientation);

void Stop();
bool IsRunning();

// 运行期更新屏幕尺寸与朝向 (系统旋转时调用)
// 只改变坐标换算基准, 不重开设备
void SetScreenSize(int screenW, int screenH, int orientation);

// 每帧调用一次: 把累积的触摸状态投递给 ImGui 事件队列
void Pump();

// 当前指针是否按下 (排障用)
bool IsDown();

// 当前指针屏幕坐标 (排障用)
void GetPos(float* x, float* y);

// 可选: 额外回调, 在每帧最后一次坐标更新时触发
void SetMoveCallback(std::function<void(float, float, bool)> cb);

} // namespace touch
} // namespace charlie
