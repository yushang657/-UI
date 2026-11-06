// ============================================================
// Charlie UI · 音量键监听
// 修正参考实现的三个问题:
//   1. 原版 while(true) + 每轮只 read 一次 -> 丢事件, CPU 100%
//      这里 select(50ms 超时) + 内层 while 抽干, 无事件时休眠
//   2. 原版无退出条件 -> 进程永不结束
//      这里 atomic<bool> 停止标志 + Join()
//   3. 原版把业务写死在监听循环里 -> 不可复用
//      这里用回调, 监听器与业务解耦
// ============================================================
#pragma once

#include <functional>

namespace charlie {

// 按键事件
struct VolumeKeyEvent {
    enum Kind { VolumeUp, VolumeDown } kind;
    bool down;    // true=按下, false=抬起 (只关心按下时可忽略)
    int  repeat;  // 长按重复计数, 0=首次
};

namespace volume {

// 回调: 在监听线程内被调用, 不要在里面做重活
using Callback = std::function<void(const VolumeKeyEvent&)>;

// 启动监听。返回 false 表示无可用输入设备 (通常是权限不足)
bool Start(Callback cb);

// 停止并回收线程
void Stop();

bool IsRunning();

// 统计: 成功打开的输入设备数 (排障用)
int OpenDeviceCount();

} // namespace volume
} // namespace charlie
