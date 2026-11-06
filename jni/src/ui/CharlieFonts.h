// ============================================================
// Charlie UI · 字体
// ============================================================
#pragma once

namespace charlie {
namespace fonts {

enum FontId {
    FONT_CAESAR = 0,    // 凯撒金 (内置)
    FONT_NOTO,          // 思源黑 (内置)
    FONT_PINGFANG,      // 苹方  (内置)
    FONT_SYS_YAHEI,     // 雅黑  (设备 /system/fonts 黑体)
    FONT_SYS_SONG,      // 宋体  (设备 /system/fonts 衬线)
    FONT_COUNT
};

// ============================================================
// 一次性装载全部字体
//
// 【为什么改成「全装」】
//   旧实现切换字体时只做 AddFontFromMemoryTTF 往图集里追加一个字体,
//   既没清旧字体, 也没重建 GPU 纹理, 结果:
//     1. 图集里字体越堆越多, 每次切换都多占一份全字库显存;
//     2. 新加的字体根本没被上传成纹理, 渲染仍然用旧的那张;
//     3. 没有任何地方去改 io.FontDefault, 所以画面上永远是第一个字体
//        —— 这就是「切换无效」的根因。
//   参考工程 查理王者 draw.cpp:1067-1072 的做法是一开始就把
//   默认字 + 雅黑 + 宋体 + 楷体 全 AddFont, 切换时只换指针。
//   这里照同样思路, 但用 io.FontDefault 而不是 PushFont,
//   这样 GetFont() / foreground drawlist 全部自动跟随。
//
// 【代价】
//   每套 CJK-Full 图集约 4096x4096 单通道 = 16 MB。
//   四套相邻排列时试图集宽 16384 超出 GL_MAX_TEXTURE_SIZE,
//   会自动换行排成 16384x16384 —— GLES3 上限正好 16384,
//   贴边走但可行。整套约 256 MB 显存, 一次性, 常驻。
// ============================================================
bool LoadAll();

// 切换当前字体: 只改 io.FontDefault, 不动图集, 不需要重建纹理。
bool SetActive(int id);

int         CurrentId();
bool        Available(int id);
const char* DisplayName(int id);
const char* SourcePath();     // 当前生效字体的来源 (含文件路径或内置标签)

// ImGui 上下文重建后必须调用: 图集随上下文销毁, 所有 ImFont* 悬空。
void Reset();

// 配置迁移: 旧版 FONT_DEFAULT(6) 已删除, 映射到 FONT_CAESAR。
int  MigrateId(int id);

} // namespace fonts
} // namespace charlie
