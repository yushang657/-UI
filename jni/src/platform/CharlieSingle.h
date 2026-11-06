// ============================================================
// Charlie UI · 单实例互斥
//
// 目的: 同一时刻只允许一个 charlie_ui 进程运行。
// 若检测到旧进程, 询问用户是关闭旧进程继续, 还是退出本次启动。
//
// 实现要点:
//   1. 用 /proc 扫描同名进程, 不依赖外部命令 (ps/pgrep 可能不存在)
//   2. 排除自身 PID
//   3. 通过 /proc/<pid>/exe 校验真实可执行路径, 避免同名误杀
//   4. 关闭旧进程用 SIGTERM, 等 1.5s, 未退再 SIGKILL
//   5. 全程只用只读接口 + signal, 不写系统目录
// ============================================================
#pragma once

namespace charlie {
namespace single {

// 检测并处理已有实例。
// 返回 true  = 可以继续启动 (无旧进程, 或用户选择关闭了旧进程)
// 返回 false = 应该退出本次启动 (用户选择保留旧进程)
//
// 无旧进程时直接返回 true, 不打印多余内容。
bool EnsureSingleInstance(const char* progName = "charlie_ui");

// 仅检测, 不做处理。返回找到的同名进程数 (不含自身)。
int CountRunning(const char* progName = "charlie_ui");

// 终止所有同名旧进程 (不含自身)。返回成功送出的信号数。
int KillOthers(const char* progName = "charlie_ui");

} // namespace single
} // namespace charlie
