// ============================================================
// Charlie UI · 字体
// ============================================================
#pragma once

namespace charlie {
namespace fonts {

enum FontId {
    FONT_CAESAR = 0,    // 凯撒金 (内置 2.0MB)
    FONT_NOTO,          // 思源黑 (内置 8.0MB)
    FONT_PINGFANG,      // 苹方  (内置 887KB)
    FONT_SYS_YAHEI,     // 雅黑  (系统字体, 机型缺失时回退)
    FONT_SYS_SONG,      // 宋体  (系统字体, 机型缺失时回退)
    FONT_SYS_KAI,       // 楷体  (系统字体, 机型缺失时回退)
    FONT_DEFAULT,       // 内置默认 (无中文, 仅调试用)
    FONT_COUNT
};

bool Load();                  // 按当前档位加载 (幂等)
bool LoadWith(int id);        // 指定档位加载
bool Loaded();

int  CurrentId();
const char* SourcePath();
const char* DisplayName(int id);

// 该档位在本次运行中是否真的可用 (系统字体在部分机型上不存在)
bool SysAvailable(int id);

// ImGui 上下文重建后必须调用, 清掉所有 ImFont* (它们全指向旧 atlas)
void Reset();

} // namespace fonts
} // namespace charlie
