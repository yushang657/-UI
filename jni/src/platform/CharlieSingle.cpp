// ============================================================
// Charlie UI · 单实例互斥实现
// ============================================================
#include "CharlieSingle.h"

#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

// popen/pclose 声明在 stdio.h 里, 但 Android NDK 需要显式引入
// 才保证在 c++_static 下可见 (否则报 "popen was not declared")。
#include <stdio.h>

namespace charlie {
namespace single {

namespace {

// 读 /proc/<pid>/comm 或解析 /proc/<pid>/cmdline 判断是否目标进程
//
// 匹配规则(对齐 `pkill -f charlie_ui.sh` 的语义):
//   pkill -f 是"整条命令行做子串匹配", 不只看 argv[0]。
//   我们这里做等价判定, 覆盖三种启动形态:
//     1) 直接执行二进制      ./charlie_ui            -> cmdline[0] basename = charlie_ui
//     2) 脚本启动            sh run.sh / sh charlie_ui.sh
//                            -> cmdline 含 "charlie_ui.sh" 或 "run.sh"
//     3) 被包在 sh -c 里     sh -c "cd /data/local/tmp && ./charlie_ui"
//                            -> cmdline 里出现 charlie_ui
//
// 只用 basename == progName 会漏掉 2) 3):
// 脚本方式的 argv[0] 是 "sh", 旧逻辑完全不认 -> 检测不到旧进程 ->
// 两个实例抢同一个 SurfaceFlinger 层。
bool ProcMatches(pid_t pid, const char* progName, std::string* exePathOut) {
    // ---- 1. /proc/<pid>/cmdline (完整命令行, \0 分隔) ----
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char buf[1024] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;

    // 把 \0 分隔的 argv 拼成一条以空格分隔的命令行, 等价 pkill -f 的匹配串
    std::string fullCmd;
    for (size_t i = 0; i < n; ++i) {
        fullCmd += (buf[i] == '\0') ? ' ' : buf[i];
    }
    // 去掉尾部多余空格
    while (!fullCmd.empty() && fullCmd.back() == ' ') fullCmd.pop_back();

    // argv[0] 的 basename
    std::string argv0(buf);
    size_t slash = argv0.find_last_of('/');
    std::string base = (slash == std::string::npos) ? argv0 : argv0.substr(slash + 1);

    // 允许脚本名: progName 换成 .sh 后缀再匹配一次
    // 例: progName="charlie_ui" -> 额外认 "charlie_ui.sh"
    char scriptName[128];
    snprintf(scriptName, sizeof(scriptName), "%s.sh", progName);

    // 【匹配必须精确】
    // 旧实现在整条 cmdline 上做 substr 查找, 会命中所有"命令行里提到
    // charlie_ui"的进程 —— 包括启动它的 sh / toybox / su 外壳。
    // 实测: `su -c "cd /data/local/tmp && nohup sh charlie_ui.sh"` 会让
    // /system/bin/sh 与 /system/bin/toybox 的 cmdline 都含 "charlie_ui",
    // 于是被误判成旧实例, 程序启动即自我否决。
    //
    // 现在只认三种精确形态:
    //   1) argv[0] 的 basename == progName        (直接执行二进制)
    //   2) argv[0] 的 basename == progName.sh     (sh 脚本)
    //   3) 某个 argv 项的 basename == progName[.sh] (解释器 + 脚本路径)
    bool matched = false;
    if (base == progName || base == scriptName) {
        matched = true;                       // 形态 1/2: argv[0] 就是目标
    } else {
        // 形态 3: 按 \0 切分 argv, 逐项比 basename。
        // 这样 "sh /data/local/tmp/charlie_ui.sh" 能命中,
        // 而 "sh -c cd ... && nohup ..." 这种把名字藏在命令串里的不会命中。
        size_t i = 0;
        while (i < n) {
            size_t j = i;
            while (j < n && buf[j] != '\0') ++j;

            // 取出这一项 argv
            std::string arg(buf + i, j - i);
            const size_t s2 = arg.find_last_of('/');
            const std::string ab = (s2 == std::string::npos) ? arg : arg.substr(s2 + 1);
            if (ab == progName || ab == scriptName) { matched = true; break; }

            i = (j < n) ? j + 1 : n;
        }
    }

    if (!matched) {
        // 再退回用 comm 比较一次 (有些进程 cmdline 被改写)
        char cpath[64];
        snprintf(cpath, sizeof(cpath), "/proc/%d/comm", (int)pid);
        FILE* cf = fopen(cpath, "rb");
        if (!cf) return false;
        char cbuf[128] = {0};
        size_t cn = fread(cbuf, 1, sizeof(cbuf) - 1, cf);
        fclose(cf);
        if (cn == 0) return false;
        // comm 末尾有换行
        while (cn > 0 && (cbuf[cn - 1] == '\n' || cbuf[cn - 1] == '\r')) cbuf[--cn] = 0;
        if (strcmp(cbuf, progName) != 0) return false;
    }

    // ---- 2. 真实可执行路径 ----
    if (exePathOut) {
        char epath[64];
        snprintf(epath, sizeof(epath), "/proc/%d/exe", (int)pid);
        char target[512] = {0};
        ssize_t tn = readlink(epath, target, sizeof(target) - 1);
        if (tn > 0) {
            target[tn] = 0;
            *exePathOut = target;
        } else {
            exePathOut->clear();
        }
    }
    return true;
}

// 判断 pid 是否是 self 的祖先 (父/祖父/...), 用于排除启动外壳。
// 我们的启动链通常是: toybox(sh) -> su -> charlie_ui
// 这些祖先进程的 cmdline 里可能含有二进制名 (su -c "...charlie_ui..."),
// 绝不能把祖先进程当成旧实例杀掉 —— 那会连带杀掉自己的整条启动链。
bool IsAncestorOfSelf(pid_t pid) {
    if (pid <= 0) return false;
    const pid_t self = getpid();
    pid_t cur = self;
    // 向上最多走 64 层, 防御 /proc 异常导致的环
    for (int depth = 0; depth < 64; ++depth) {
        char p[64];
        snprintf(p, sizeof(p), "/proc/%d/stat", (int)cur);
        FILE* f = fopen(p, "rb");
        if (!f) return false;
        char buf[512] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0) return false;

        // stat: "pid (comm) state ppid ..."  comm 里可能含空格/括号,
        // 所以从最后一个 ')' 之后开始解析。
        const char* rp = strrchr(buf, ')');
        if (!rp) return false;
        char st = 0;
        int ppid = 0;
        if (sscanf(rp + 1, " %c %d", &st, &ppid) != 2) return false;

        if (ppid <= 0 || ppid == cur) return false;   // 到顶
        if ((pid_t)ppid == pid) return true;          // 找到目标祖先
        cur = (pid_t)ppid;
    }
    return false;
}

// 收集所有同名进程 (排除自身与祖先)
std::vector<pid_t> FindOthers(const char* progName) {
    std::vector<pid_t> out;

    DIR* d = opendir("/proc");
    if (!d) return out;

    const pid_t self = getpid();

    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        // 只处理纯数字目录
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        pid_t pid = (pid_t)atoi(e->d_name);
        if (pid <= 0 || pid == self) continue;

        if (!ProcMatches(pid, progName, nullptr)) continue;
        // 祖先进程 (sh / su / toybox) 不算旧实例
        if (IsAncestorOfSelf(pid)) continue;

        out.push_back(pid);
    }
    closedir(d);

    std::sort(out.begin(), out.end());
    return out;
}

// 判断进程是否还活着
bool Alive(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;

    // 第 3 字段是状态; Z = 僵尸, 视为已退出
    const char* rp = strrchr(buf, ')');
    if (!rp || rp[1] == 0) return true;
    char st = 0;
    if (sscanf(rp + 1, " %c", &st) == 1) {
        if (st == 'Z') return false;
    }
    return true;
}

// 从终端读取一行; 返回首字符, EOF 返回 0
char AskChoice() {
    printf("  请选择 [1=关闭旧进程继续  2=退出本次启动]: ");
    fflush(stdout);

    char line[32] = {0};
    if (!fgets(line, sizeof(line), stdin)) {
        // stdin 不可用 (后台 nohup、管道、adb shell 无 tty 等)。
        // 返回 '1' 而不是 0: 这种场景下用户根本没机会作答,
        // 默认「退出」会让程序永远无法在后台启动。
        // 语义上等价于「无人值守 -> 清理旧实例后继续」。
        printf("(stdin 不可用, 默认关闭旧进程继续)\n");
        fflush(stdout);
        return '1';
    }
    return line[0];
}

} // namespace

int CountRunning(const char* progName) {
    return (int)FindOthers(progName).size();
}

int KillOthers(const char* progName) {
    std::vector<pid_t> others = FindOthers(progName);
    if (others.empty()) return 0;

    int sent = 0;

    // 第一轮: SIGTERM, 给机会自己收尾 (保存配置/释放层)
    for (pid_t pid : others) {
        printf("[Charlie] 结束旧进程 %d ...\n", (int)pid);
        if (kill(pid, SIGTERM) == 0) sent++;
    }

    // 最多等 1.5s
    for (int i = 0; i < 30; ++i) {
        bool anyAlive = false;
        for (pid_t pid : others) {
            if (Alive(pid)) { anyAlive = true; break; }
        }
        if (!anyAlive) break;
        usleep(50 * 1000);
    }

    // 第二轮: 还活着的强杀
    for (pid_t pid : others) {
        if (Alive(pid)) {
            printf("[Charlie] 进程 %d 未响应 SIGTERM, 强制结束\n", (int)pid);
            kill(pid, SIGKILL);
        }
    }

    // 给内核一点回收时间
    usleep(200 * 1000);

    // 脚本名: progName 加 .sh 后缀, 对应 `pkill -f charlie_ui.sh`
    char scriptName[128];
    snprintf(scriptName, sizeof(scriptName), "%s.sh", progName);

    // ---------- 第三轮: 命令行兜底 ----------
    // 上面 FindOthers 依赖 /proc 扫描 + cmdline 解析, 覆盖的是
    // "能读到 cmdline" 的进程。但脚本启动方式下可能出现:
    //   - 脚本进程本身还挂着 (sh charlie_ui.sh 的父进程)
    //   - 二进制已被 kill, 但脚本仍在 tee/等待子进程
    // 这两种情况 cmdline 里未必命中 progName 前缀规则。
    //
    // 用 pkill -f 做一次按整条命令行的兜底, 与用户约定的
    //   pkill -f charlie_ui.sh
    // 保持完全一致。
    //
    // 【致命细节】pkill -f 会匹配到自己!
    // 本进程的 cmdline 就是 "charlie_ui" (或 "sh charlie_ui.sh"),
    // 直接 `pkill -f charlie_ui` 等于自杀 —— system() 里派生的 sh
    // 执行 pkill, pkill 扫到本进程并 SIGTERM 它, 于是:
    //   - 要么 pkill 先杀父进程再杀 shell, 程序当场消失
    //   - 要么留下半个死的状态
    // 正确写法是排除自身 PID。pkill 没有 --exclude-pid 选项,
    // 所以用 "-o"? 不行(那是最老的)。
    // 这里改成继续走 execl 不可行, 用 pgrep 取列表再逐个 kill 排除自身。
    //
    // 注意: progName 本身也可能是 shell 脚本名, 因此两种模式都要排除。
    {
        const pid_t selfPid = getpid();

        // pgrep -f <pattern> 列出所有匹配 PID, 在用户态过滤掉自己
        for (int pass = 0; pass < 2; ++pass) {
            const char* pattern = (pass == 0) ? scriptName : progName;

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "pgrep -f %s 2>/dev/null", pattern);
            FILE* pf = popen(cmd, "r");
            if (!pf) continue;

            std::vector<pid_t> hits;
            char line[64];
            while (fgets(line, sizeof(line), pf)) {
                pid_t p = (pid_t)atoi(line);
                if (p > 0 && p != selfPid) hits.push_back(p);
            }
            pclose(pf);

            for (pid_t p : hits) {
                if (!Alive(p)) continue;
                kill(p, SIGTERM);
            }
        }
    }

    usleep(150 * 1000);

    // 二次强杀: 仍有残留则 SIGKILL (同样排除自身)
    {
        const pid_t selfPid = getpid();
        for (int pass = 0; pass < 2; ++pass) {
            const char* pattern = (pass == 0) ? scriptName : progName;

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "pgrep -f %s 2>/dev/null", pattern);
            FILE* pf = popen(cmd, "r");
            if (!pf) continue;

            std::vector<pid_t> hits;
            char line[64];
            while (fgets(line, sizeof(line), pf)) {
                pid_t p = (pid_t)atoi(line);
                if (p > 0 && p != selfPid) hits.push_back(p);
            }
            pclose(pf);

            for (pid_t p : hits) {
                if (!Alive(p)) continue;
                printf("[Charlie] 残留进程 %d, 强制结束\n", (int)p);
                kill(p, SIGKILL);
            }
        }
    }

    usleep(100 * 1000);

    printf("[Charlie] 旧进程已清理\n");
    return sent;
}

bool EnsureSingleInstance(const char* progName) {
    std::vector<pid_t> others = FindOthers(progName);
    if (others.empty()) return true;   // 无旧进程, 直接继续

    // ---------- 提示用户 ----------
    printf("\n");
    printf("==================================================\n");
    printf("  Charlie UI 已在运行\n");
    printf("--------------------------------------------------\n");
    printf("  检测到 %d 个同名进程:\n", (int)others.size());
    for (pid_t pid : others) {
        std::string exe;
        ProcMatches(pid, progName, &exe);
        if (!exe.empty()) {
            printf("    PID %-7d %s\n", (int)pid, exe.c_str());
        } else {
            printf("    PID %-7d (路径不可读)\n", (int)pid);
        }
    }
    printf("--------------------------------------------------\n");
    printf("  1 = 关闭旧进程, 继续本次启动\n");
    printf("  2 = 退出本次启动 (保留旧进程)\n");
    printf("==================================================\n");

    char c = AskChoice();

    if (c == '1') {
        printf("\n[Charlie] 正在关闭旧进程...\n");
        KillOthers(progName);

        // 复核: 确认真的清干净了
        int left = CountRunning(progName);
        if (left > 0) {
            printf("[Charlie] 仍有 %d 个进程, 本次启动终止\n", left);
            return false;
        }
        printf("[Charlie] 继续启动\n\n");
        return true;
    }

    // 默认/选择 2: 退出
    printf("\n[Charlie] 已取消本次启动, 旧进程继续运行\n");
    return false;
}

} // namespace single
} // namespace charlie
