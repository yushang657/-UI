// ============================================================
// Charlie UI · 字体实现
//
// 【本轮重写要点: 修复「切换字体无效」】
//
//   旧实现的切换路径是:
//       fonts::Reset();            // 只清了一个 bool 标志
//       fonts::LoadWith(id);       // 往图集里再 Add 一个字体
//   两个致命问题:
//     a) 图集已经 Build 过, 再加字体不会重排已有字形,
//        新字体也没被 GetTexDataAsRGBA32 重新上传 —— GPU 上仍是旧纹理;
//     b) 全程没人改 io.FontDefault, 所以 GetFont() 永远返回 Fonts[0]。
//   表现就是「点了没反应」。
//
//   现在改成 查理王者 draw.cpp:1067-1072 的思路:
//     LoadAll() 一次性把 3 内置 + 2 系统 全部 AddFontFrom*,
//     之后 SetActive(id) 只做 io.FontDefault = g_font[id]。
//   图集只 Build 一次 (由 backend 的 NewFrame 负责上传纹理),
//   切换零开销、零分配。
//
// 【字体清单: 只保留 查理王者/ 里有的】
//   凯撒金 / 思源黑 / 苹方  —— 内置, 打包进 .rodata
//   雅黑 (黑体)             —— /system/fonts/NotoSansCJK-Regular.ttc
//   宋体 (衬线)             —— /system/fonts/NotoSerifCJK-Regular.ttc
//   「内置默认 ProggyClean」「楷体」已删除:
//     ProggyClean 无中文, 选了只会看到方块;
//     楷体在查理王者里其实映射到 NotoSerifCJK, 与宋体重复。
// ============================================================
#include "CharlieFonts.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------- 内置字体符号 (由 assets/*.o 提供) ----------
extern "C" {
extern const unsigned char _binary_caesar_ttf_start[];
extern const unsigned char _binary_caesar_ttf_end[];
extern const unsigned char _binary_font_ttf_start[];
extern const unsigned char _binary_font_ttf_end[];
extern const unsigned char _binary_pingfang_ttf_start[];
extern const unsigned char _binary_pingfang_ttf_end[];
}

namespace charlie {
namespace fonts {

namespace {

// ============================================================
// 【图集体积是本模块最大的坑】
//
//   GetGlyphRangesChineseFull() 覆盖 CJK 统一表意区 + 扩展 A,
//   共约 21000 个码位。在 31px + RasterizerDensity 2.0 下,
//   单个字形按 62px 栅格化, 一套字体就要 4096x4096 单通道 = 16 MB。
//
//   五套字体相邻排列时, ImGui 试图集宽 16384 放一行,
//   放不下就换成两行 —— 图集变成 16384x16384 = 256 MB。
//   Mali-G68 上实测: 上传这张纹理时驱动在地址计算里溢出,
//   直接 SIGSEGV (tombstone_30, fault addr 0xb3fff87b48600000)。
//   进程在"进入主循环"之后立刻死, 日志里看不到任何 Dart/GL 错误。
//
// 【对策: 按界面实际用字构建字形范围】
//   不再用 ImGui 内置的 ChineseFull (21000) 或 SimplifiedCommon (2500),
//   而是把源码里所有字符串字面量的非 ASCII 字符提出来 (405 字),
//   用 ImFontGlyphRangesBuilder 合并成紧缩区间。
//   五套字体合计不到 2 MB, 图集 2048x2048 就够, 且不会缺字。
//
//   SimplifiedCommon 那条路已经证明是错的: 它的表来自 1987 年新闻语料,
//   界面里的「瞄/陀/螺/椎/盆/阈/帧/渲/抖/雾」全在表外,
//   运行时表现为大片 '?'。详见 UiRanges() 的注释。
// ============================================================

// ============================================================
// 基准字号
//
// 【为什么是 31】
//   参考工程 draw.cpp:1069 用
//       AddFontFromMemoryTTF(font_v, font_v_size, 31.0f, ...)
//   31px 是它整套 UI 的排版基准。我们跟它对齐, 中文在 3x 屏上
//   笔画展开到 2~3 物理像素, 抗锯齿后边缘才立得住。
//
// 【为什么还要 RasterizerDensity】
//   所有控件字号是按 metrics.s 缩放的 (m.s ≈ 1.0~1.55),
//   实际渲染字号会在 22~48px 之间浮动。图集只在 31px 栅格化的话,
//   渲染成 48px 就是位图放大 -> 发虚。
//   density 让图集按 31*2 = 62px 栅格化, 渲染时缩小取样,
//   于是所有字号都清晰。代价是图集内存 x4, 一次性开销。
// ============================================================
constexpr float kSizePx  = 31.f;
constexpr float kDensity = 2.0f;

bool g_loaded  = false;
int  g_current = FONT_CAESAR;
char g_path[320] = "";

// 每档的 ImFont*; nullptr = 该档在本机不存在
ImFont* g_font[FONT_COUNT] = {};
char    g_src [FONT_COUNT][192] = {};

bool FileReadable(const char* p) {
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// ============================================================
// 界面实际用到的字符集
//
// 【为什么不能直接用 GetGlyphRangesChineseSimplifiedCommon()】
//   那张表是 1987 年 7 月新闻语料里出现频率前 2500 的汉字。
//   我们的界面全是游戏/调试术语:
//     瞄 陀 螺 仪 椎 盆 骨 骼 阈 值 帧 率 渲 染 掉 帧 抖 枪 雾 天 ...
//   这些字在新闻语料里根本排不进前 2500, 于是图集里没有它们的字形。
//   ImFont::BuildLookupTable 找不到码位就把 IndexAdvanceX 指向
//   FallbackGlyph, 而 FallbackGlyph 正是 GetGlyphRangesDefault() 里的
//   '?' (U+003F)。表现就是整块界面到处是问号。
//
// 【改为按实际用字精确构建】
//   kUiText 是把源码所有字符串字面量里的非 ASCII 字符
//   去重排序后拼成的 UTF-8 串 (共 405 字), 由脚本生成。
//   ImFontGlyphRangesBuilder 会把它合并成紧缩的码位区间。
//
//   【维护约定】
//   新增界面文案后要同步这个表, 否则新字会显示成 '?'。
//   生成脚本逻辑: 扫描 jni/src/**/*.cpp 与 *.h 的字符串字面量,
//   剔除注释, 提取所有 >= 0x80 的码位, 排序去重,
//   以 UTF-8 转义写入下方 kUiText。
//   这个表只影响"有什么字形", 不影响图集体积的阶跃 ——
//   405 字 vs 2500 字, 图集反而更小。
// ============================================================
static const char kUiText[] =

    "\xc2\xb0\xc2\xb7\xc3\x97\xe3\x80\x81\xe3\x80\x82"
    "\xe3\x80\x8c\xe3\x80\x8d\xe4\xb8\x80\xe4\xb8\x8a"
    "\xe4\xb8\x8b\xe4\xb8\x8d\xe4\xb8\x8e\xe4\xb8\xa4"
    "\xe4\xb8\xaa\xe4\xb8\xad\xe4\xb8\xba\xe4\xb8\xbb"
    "\xe4\xba\x8e\xe4\xba\x92\xe4\xba\xa4\xe4\xbb\x85"
    "\xe4\xbb\x96\xe4\xbb\xaa\xe4\xbb\xb6\xe4\xbb\xbb"
    "\xe4\xbc\x8d\xe4\xbc\x98\xe4\xbc\x9a\xe4\xbd\x8d"
    "\xe4\xbd\x93\xe4\xbd\x95\xe4\xbd\x99\xe4\xbd\xa0"
    "\xe4\xbd\xbf\xe4\xbe\xa7\xe4\xbf\x9d\xe4\xbf\xa1"
    "\xe4\xbf\xae\xe5\x80\x92\xe5\x80\xbc\xe5\x85\x85"
    "\xe5\x85\x88\xe5\x85\x89\xe5\x85\x8d\xe5\x85\xa5"
    "\xe5\x85\xa8\xe5\x85\xb3\xe5\x85\xb6\xe5\x86\x85"
    "\xe5\x86\x8d\xe5\x86\x99\xe5\x87\x86\xe5\x87\xaf"
    "\xe5\x87\xba\xe5\x87\xbb\xe5\x88\x86\xe5\x88\x87"
    "\xe5\x88\x97\xe5\x88\x99\xe5\x88\x9b\xe5\x88\x9d"
    "\xe5\x88\xa4\xe5\x88\xb0\xe5\x88\xb6\xe5\x88\xb7"
    "\xe5\x89\x8d\xe5\x8a\xa0\xe5\x8a\xa8\xe5\x8a\xa9"
    "\xe5\x8c\x85\xe5\x8c\x96\xe5\x8c\xba\xe5\x8d\x95"
    "\xe5\x8d\xa0\xe5\x8d\xb3\xe5\x8f\x82\xe5\x8f\x96"
    "\xe5\x8f\xa3\xe5\x8f\xaa\xe5\x8f\xaf\xe5\x8f\xb3"
    "\xe5\x8f\xb7\xe5\x90\x88\xe5\x90\x8c\xe5\x90\x8d"
    "\xe5\x90\x8e\xe5\x90\x91\xe5\x90\xab\xe5\x90\xaf"
    "\xe5\x90\xb8\xe5\x93\x8d\xe5\x96\x84\xe5\x99\xa8"
    "\xe5\x9b\x9b\xe5\x9b\x9e\xe5\x9b\xb4\xe5\x9b\xba"
    "\xe5\x9b\xbe\xe5\x9c\x86\xe5\x9c\x88\xe5\x9c\xa8"
    "\xe5\x9c\xb0\xe5\x9d\x87\xe5\x9d\x97\xe5\x9e\x8b"
    "\xe5\x9f\xba\xe5\xa1\xab\xe5\xa3\xb0\xe5\xa4\x96"
    "\xe5\xa4\xa7\xe5\xa4\xb1\xe5\xa4\xb4\xe5\xa5\x8f"
    "\xe5\xa5\x97\xe5\xa7\x8b\xe5\xad\x97\xe5\xad\x98"
    "\xe5\xae\x8b\xe5\xae\x8c\xe5\xae\x9a\xe5\xae\x9e"
    "\xe5\xaf\xb8\xe5\xaf\xbc\xe5\xb0\x84\xe5\xb0\xb1"
    "\xe5\xb0\xba\xe5\xb1\x80\xe5\xb1\x82\xe5\xb1\x8f"
    "\xe5\xb2\x9b\xe5\xb3\xb0\xe5\xb7\xa5\xe5\xb7\xa6"
    "\xe5\xb7\xb2\xe5\xb8\xa7\xe5\xb8\xb8\xe5\xb9\x95"
    "\xe5\xb9\xb2\xe5\xb9\xb3\xe5\xba\x8f\xe5\xba\xa6"
    "\xe5\xba\xa7\xe5\xbb\xba\xe5\xbc\x80\xe5\xbc\x8f"
    "\xe5\xbc\xb9\xe5\xbc\xba\xe5\xbd\x93\xe5\xbd\xa2"
    "\xe5\xbd\xb1\xe5\xbe\xaa\xe5\xbf\xab\xe5\xbf\xbd"
    "\xe6\x80\x81\xe6\x80\x9d\xe6\x80\xa7\xe6\x80\xbb"
    "\xe6\x81\xaf\xe6\x82\xac\xe6\x88\x8f\xe6\x88\x90"
    "\xe6\x88\x91\xe6\x88\x96\xe6\x88\x98\xe6\x89\x80"
    "\xe6\x89\x87\xe6\x89\x8d\xe6\x89\x93\xe6\x89\xb0"
    "\xe6\x89\xb3\xe6\x89\xbe\xe6\x8a\x96\xe6\x8b\x96"
    "\xe6\x8b\x9f\xe6\x8b\xa9\xe6\x8b\xbe\xe6\x8c\x89"
    "\xe6\x8d\xa2\xe6\x8d\xae\xe6\x8d\xb7\xe6\x8e\xa7"
    "\xe6\x8f\x8f\xe6\x8f\x90\xe6\x91\xb8\xe6\x92\x92"
    "\xe6\x94\xb9\xe6\x94\xbe\xe6\x95\x88\xe6\x95\xb0"
    "\xe6\x95\xb4\xe6\x96\x87\xe6\x96\x97\xe6\x96\xb0"
    "\xe6\x96\xb9\xe6\x97\xa0\xe6\x97\xa5\xe6\x97\xb6"
    "\xe6\x98\x8e\xe6\x98\xbe\xe6\x99\x93\xe6\x9a\x97"
    "\xe6\x9c\x80\xe6\x9c\x89\xe6\x9c\x9f\xe6\x9c\xaa"
    "\xe6\x9c\xac\xe6\x9c\xba\xe6\x9d\x83\xe6\x9d\x9f"
    "\xe6\x9d\xbf\xe6\x9e\x84\xe6\x9e\x90\xe6\x9e\xaa"
    "\xe6\x9f\x93\xe6\x9f\xa5\xe6\xa0\x87\xe6\xa0\x88"
    "\xe6\xa0\x8f\xe6\xa0\xb7\xe6\xa1\x86\xe6\xa1\x88"
    "\xe6\xa1\xa3\xe6\xa3\x80\xe6\xa4\x8e\xe6\xa6\x82"
    "\xe6\xa8\xa1\xe6\xac\xa1\xe6\xad\xa3\xe6\xad\xa5"
    "\xe6\xad\xa6\xe6\xaf\x94\xe6\xb3\x95\xe6\xb3\xa8"
    "\xe6\xb4\xbb\xe6\xb5\x81\xe6\xb5\x85\xe6\xb5\xae"
    "\xe6\xb7\xb1\xe6\xb8\xb2\xe6\xb8\xb8\xe6\xba\x90"
    "\xe6\xbb\x91\xe6\xbc\x94\xe7\x81\xab\xe7\x81\xb5"
    "\xe7\x82\xb9\xe7\x83\x9f\xe7\x89\x88\xe7\x89\xb9"
    "\xe7\x8a\xb6\xe7\x8b\xac\xe7\x8e\x87\xe7\x8e\xaf"
    "\xe7\x94\x9f\xe7\x94\xa8\xe7\x94\xb1\xe7\x94\xb5"
    "\xe7\x94\xbb\xe7\x95\x85\xe7\x95\x8c\xe7\x95\xa5"
    "\xe7\x99\xbe\xe7\x9a\x84\xe7\x9b\x86\xe7\x9b\xae"
    "\xe7\x9c\x81\xe7\x9e\x84\xe7\x9f\xa5\xe7\x9f\xa9"
    "\xe7\xa1\x80\xe7\xa1\xae\xe7\xa4\xba\xe7\xa6\xbb"
    "\xe7\xa7\xb0\xe7\xa7\xbb\xe7\xa8\x8b\xe7\xa9\xba"
    "\xe7\xa9\xbf\xe7\xaa\x97\xe7\xab\x8b\xe7\xab\xaf"
    "\xe7\xb1\xb3\xe7\xb1\xbb\xe7\xb2\x97\xe7\xb3\xbb"
    "\xe7\xba\xaf\xe7\xba\xb5\xe7\xba\xbf\xe7\xbb\x86"
    "\xe7\xbb\x93\xe7\xbb\x98\xe7\xbb\x9c\xe7\xbb\x9f"
    "\xe7\xbb\xa7\xe7\xbb\xaa\xe7\xbb\xad\xe7\xbc\x98"
    "\xe7\xbc\xa9\xe7\xbd\x91\xe7\xbd\xae\xe8\x80\x97"
    "\xe8\x82\xa4\xe8\x83\xb8\xe8\x83\xbd\xe8\x84\x8a"
    "\xe8\x87\xaa\xe8\x88\xaa\xe8\x89\xb2\xe8\x8a\x82"
    "\xe8\x8b\xb9\xe8\x8c\x83\xe8\x8d\xaf\xe8\x90\xbd"
    "\xe8\x97\x8f\xe8\x9e\xba\xe8\xa1\x80\xe8\xa1\x8c"
    "\xe8\xa1\xa1\xe8\xa1\xa8\xe8\xa2\xab\xe8\xa3\x85"
    "\xe8\xa7\x81\xe8\xa7\x82\xe8\xa7\x86\xe8\xa7\x88"
    "\xe8\xa7\x89\xe8\xa7\x92\xe8\xa7\xa3\xe8\xa7\xa6"
    "\xe8\xae\xa1\xe8\xae\xa4\xe8\xae\xb0\xe8\xae\xbe"
    "\xe8\xaf\x81\xe8\xaf\x95\xe8\xaf\xa5\xe8\xaf\xb4"
    "\xe8\xaf\xbb\xe8\xb0\x83\xe8\xb4\xa3\xe8\xb4\xa5"
    "\xe8\xb5\x84\xe8\xb6\x85\xe8\xb7\x9d\xe8\xb7\x9f"
    "\xe8\xb7\xb3\xe8\xbd\xbd\xe8\xbe\x85\xe8\xbe\x91"
    "\xe8\xbe\xb9\xe8\xbf\x87\xe8\xbf\x90\xe8\xbf\x9b"
    "\xe8\xbf\x9e\xe8\xbf\xb0\xe9\x80\x80\xe9\x80\x89"
    "\xe9\x80\x8f\xe9\x80\x9f\xe9\x80\xbb\xe9\x81\x93"
    "\xe9\x83\xa8\xe9\x85\x8d\xe9\x87\x87\xe9\x87\x8d"
    "\xe9\x87\x8e\xe9\x87\x8f\xe9\x87\x91\xe9\x92\x9f"
    "\xe9\x92\xae\xe9\x94\x81\xe9\x94\xae\xe9\x95\x9c"
    "\xe9\x97\xaa\xe9\x97\xad\xe9\x97\xb4\xe9\x98\x88"
    "\xe9\x98\x9f\xe9\x98\xb2\xe9\x99\x80\xe9\x99\x84"
    "\xe9\x99\x8d\xe9\x99\x90\xe9\x9a\x8f\xe9\x9a\x90"
    "\xe9\x9a\x94\xe9\x9b\x85\xe9\x9b\x86\xe9\x9b\xbe"
    "\xe9\x9d\xa2\xe9\x9f\xb3\xe9\xa1\xb5\xe9\xa1\xb9"
    "\xe9\xa1\xbb\xe9\xa2\x84\xe9\xa2\x88\xe9\xa2\x98"
    "\xe9\xa9\xb1\xe9\xa9\xbb\xe9\xaa\x8c\xe9\xaa\xa8"
    "\xe9\xaa\xbc\xe9\xbb\x91\xe9\xbb\x98\xef\xbc\x8c";;

// 合并后的码位区间; BuildRanges 之后以 0 结尾
static ImVector<ImWchar> g_ranges;

// 取当前字体图集应当装载的码位范围。
// 首次调用时构建, 之后复用 (区间表与 atlas 生命周期无关)。
const ImWchar* UiRanges(ImFontAtlas* atlas) {
    if (g_ranges.empty()) {
        ImFontGlyphRangesBuilder b;
        // 基础 ASCII: 数字/单位/百分号/中括号全在这里
        b.AddRanges(atlas->GetGlyphRangesDefault());
        // 界面用到的全部中文与符号
        b.AddText(kUiText);
        b.BuildRanges(&g_ranges);
        printf("[Charlie] font: 字形范围 %d 段, "
               "覆盖界面用字 %d 个\n",
               (g_ranges.Size - 1) / 2, (int)sizeof(kUiText) - 1);
    }
    return g_ranges.Data;
}

ImFontConfig MakeConfig() {
    ImFontConfig cfg;
    // Oversample 与 RasterizerDensity 是两套机制, 不能同时拉满:
    // density 已经提供了超出渲染尺寸的采样密度, 再叠 Oversample 3
    // 只会让图集指数膨胀而不增加可见清晰度。
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH  = false;
    // 轻微加权重采样: 汉字笔画细, 1.06 让灰度过渡更实一点。
    // 调太高 ( >1.15 ) 会让字重看起来像假粗体。
    cfg.RasterizerMultiply = 1.08f;
    // 关键: 按 2x 栅格化, 渲染时缩小取样 -> 所有字号都清晰
    cfg.RasterizerDensity  = kDensity;
    return cfg;
}

// ---------- 内置字体源 (编译期打包进 .rodata) ----------
void AddBuiltin(ImFontAtlas* atlas, int id, const ImWchar* rng) {
    const unsigned char* b = nullptr;
    const unsigned char* e = nullptr;
    const char* label = "";

    switch (id) {
    case FONT_CAESAR:   b = _binary_caesar_ttf_start;   e = _binary_caesar_ttf_end;   label = "凯撒金"; break;
    case FONT_NOTO:     b = _binary_font_ttf_start;     e = _binary_font_ttf_end;     label = "思源黑"; break;
    case FONT_PINGFANG: b = _binary_pingfang_ttf_start; e = _binary_pingfang_ttf_end; label = "苹方";   break;
    default: return;
    }

    const size_t size = (size_t)(e - b);
    if (size < 1024) {
        printf("[Charlie] font: %s 数据为空, 跳过\n", label);
        return;
    }

    ImFontConfig cfg = MakeConfig();
    cfg.FontDataOwnedByAtlas = false;   // 数据在 .rodata, 不能释放

    ImFont* f = atlas->AddFontFromMemoryTTF((void*)b, (int)size, kSizePx, &cfg, rng);
    if (f) {
        g_font[id] = f;
        snprintf(g_src[id], sizeof(g_src[id]), "%s (内置 %zu KB)", label, size / 1024);
        printf("[Charlie] font[%d]: %s  %.0fpx density %.1f\n", id, g_src[id], kSizePx, kDensity);
    } else {
        printf("[Charlie] font: %s 解析失败\n", label);
    }
}

// ---------- 系统字体 ----------
//
// 路径候选照搬 查理王者 draw.cpp:991-999, 并按本机实际字体表校正:
//   OP-PJY110 上 MiSans-Regular.ttf / NotoSansCJK-Regular.ttf /
//   DroidSansFallback.ttf 均不存在, 只有 .ttc 形式。
// 所以黑体档首位仍留 MiSans (小米机型), 真正命中靠 .ttc。
//
// 宋体档去掉了 NotoSerifSC-Regular.otf —— 查理王者里有, 但本机没有,
// 留着只是多一次 fopen 失败。
void AddSystem(ImFontAtlas* atlas, int id, const ImWchar* rng) {
    const char* paths[5] = {};
    const char* label = "";

    if (id == FONT_SYS_YAHEI) {
        label = "雅黑";
        paths[0] = "/system/fonts/MiSans-Regular.ttf";
        paths[1] = "/system/fonts/NotoSansCJK-Regular.ttc";
        paths[2] = "/system/fonts/DroidSansFallback.ttf";
        paths[3] = "/system/fonts/SysSans-Hans-Regular.ttf";
    } else if (id == FONT_SYS_SONG) {
        label = "宋体";
        paths[0] = "/system/fonts/NotoSerifCJK-Regular.ttc";
        paths[1] = "/system/fonts/NotoSerifCJK-Regular.ttf";
    } else {
        return;
    }

    for (int i = 0; i < 5 && paths[i]; ++i) {
        if (!FileReadable(paths[i])) continue;
        ImFontConfig cfg = MakeConfig();
        ImFont* f = atlas->AddFontFromFileTTF(paths[i], kSizePx, &cfg, rng);
        if (f) {
            g_font[id] = f;
            snprintf(g_src[id], sizeof(g_src[id]), "%s (%s)", label, paths[i]);
            printf("[Charlie] font[%d]: %s\n", id, g_src[id]);
            return;
        }
        printf("[Charlie] font: %s 打开失败, 试下一个\n", paths[i]);
    }
    printf("[Charlie] font: %s 本机不可用, 该档隐藏\n", label);
}

} // namespace

// ============================================================
// 对外接口
// ============================================================

const char* DisplayName(int id) {
    switch (id) {
    case FONT_CAESAR:    return "凯撒金";
    case FONT_NOTO:      return "思源黑";
    case FONT_PINGFANG:  return "苹方";
    case FONT_SYS_YAHEI: return "雅黑";
    case FONT_SYS_SONG:  return "宋体";
    default:             return "未知";
    }
}

bool Available(int id) {
    if (id < 0 || id >= FONT_COUNT) return false;
    // 系统档还没装载时, 用文件可读性做探测 —— 设置页要据此决定列不列出来。
    if (id == FONT_SYS_YAHEI) {
        return FileReadable("/system/fonts/MiSans-Regular.ttf")
            || FileReadable("/system/fonts/NotoSansCJK-Regular.ttc")
            || FileReadable("/system/fonts/DroidSansFallback.ttf")
            || FileReadable("/system/fonts/SysSans-Hans-Regular.ttf");
    }
    if (id == FONT_SYS_SONG) {
        return FileReadable("/system/fonts/NotoSerifCJK-Regular.ttc")
            || FileReadable("/system/fonts/NotoSerifCJK-Regular.ttf");
    }
    // 内置档: 装载成功后才有指针
    return g_font[id] != nullptr;
}

// 一次性把所有可用字体压进图集。
// 必须在第一个 NewFrame 之前调用 —— 图集只能在没 Build 之前增删字体。
bool LoadAll() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    if (!atlas) return false;

    // 幂等: 重复调用直接返回, 避免往已 Build 的图集里塞字体。
    if (g_loaded) return true;

    // Clear 会把 Fonts / TexData / InputData 一起清掉,
    // 顺带把上一轮上下文遗留的 ImFont* 全部作废。
    // 注意: 这一步只重置 CPU 侧数据, GPU 纹理由
    // backend 的 NewFrame 检测 TexID==0 后重建。
    atlas->Clear();
    for (int i = 0; i < FONT_COUNT; ++i) { g_font[i] = nullptr; g_src[i][0] = 0; }

    // 【字形范围三选一的取舍】
    //   ChineseFull       21000 码位 → 五套叠出 256 MB, Mali-G68 SIGSEGV
    //   ChineseSimplified 2500 新闻常用字 → 界面里的游戏术语大量缺字 → '?'
    //   按实际用字构建 (当前方案) → 405 字, 图集最小, 且不缺字
    //   详见 UiRanges() 上方的说明。
    const ImWchar* rng = UiRanges(atlas);

    AddBuiltin(atlas, FONT_CAESAR,    rng);
    AddBuiltin(atlas, FONT_NOTO,      rng);
    AddBuiltin(atlas, FONT_PINGFANG,  rng);
    AddSystem (atlas, FONT_SYS_YAHEI, rng);
    AddSystem (atlas, FONT_SYS_SONG,  rng);

    // 一个都没成就退到 ProggyClean, 至少界面能画出来 (中文会是方块)
    if (atlas->Fonts.Size == 0) {
        ImFontConfig cfg;
        cfg.SizePixels = 26.f;
        atlas->AddFontDefault(&cfg);
        printf("[Charlie] font: 全部失败, 回退 ProggyClean (中文显示为方块)\n");
    }

    g_loaded = true;
    printf("[Charlie] font: 装载完成, 图集合计 %d 套\n", atlas->Fonts.Size);

    return SetActive(g_current);
}

// 切换: 只改 io.FontDefault。
// ImGui 在 NewFrame 里用 g.IO.FontDefault 决定 g.Font,
// GetFont() 与 foreground drawlist 的 AddText 全部跟着变。
bool SetActive(int id) {
    id = MigrateId(id);
    ImGuiIO& io = ImGui::GetIO();

    if (id >= 0 && id < FONT_COUNT && g_font[id]) {
        io.FontDefault = g_font[id];
        g_current = id;
        snprintf(g_path, sizeof(g_path), "%s", g_src[id]);
        printf("[Charlie] font: 切换 -> %s\n", g_path);
        return true;
    }

    // 请求的档位在本机不存在: 退到第一个真正装载成功的档位。
    for (int i = 0; i < FONT_COUNT; ++i) {
        if (g_font[i]) {
            io.FontDefault = g_font[i];
            g_current = i;
            snprintf(g_path, sizeof(g_path), "%s", g_src[i]);
            printf("[Charlie] font: %s 不可用, 回退 %s\n", DisplayName(id), g_path);
            return false;
        }
    }

    io.FontDefault = nullptr;
    g_current = id;
    snprintf(g_path, sizeof(g_path), "回退默认");
    return false;
}

int  CurrentId()   { return g_current; }
const char* SourcePath() { return g_path; }

// 旧配置里 6 是「内置默认」, 该档已删除。
// 直接落到凯撒金, 避免用户升级后看到一个不存在的档位名。
int MigrateId(int id) {
    if (id < 0 || id >= FONT_COUNT) return FONT_CAESAR;
    return id;
}

// ImGui 上下文被重建后 (屏幕旋转重建渲染栈), 图集随上下文消失,
// 所有 ImFont* 全部悬空。必须整体清掉, 否则下次 SetFont 直接野指针崩。
void Reset() {
    g_loaded = false;
    g_path[0] = 0;
    for (int i = 0; i < FONT_COUNT; ++i) { g_font[i] = nullptr; g_src[i][0] = 0; }
}

} // namespace fonts
} // namespace charlie
