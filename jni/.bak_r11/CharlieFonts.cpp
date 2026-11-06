// ============================================================
// Charlie UI · 字体实现
//
// 【本次重构要点】
//   1. 基准字号 18 -> 31, 对齐参考工程「查理王者」。
//      18px 在 3x 屏上等于 18 物理像素, 汉字笔画只有 1~2px,
//      抗锯齿后基本糊在一起 —— 这就是"看不清"的根因。
//   2. 字库范围 GetGlyphRangesChineseSimplifiedCommon() -> ChineseFull()。
//      前者只有 2500 个常用字, 配置里写「帧率」「悬浮窗」这类词没事,
//      但用户自定义名称、生僻字会直接变成方块。
//   3. 引入 ImFontConfig::RasterizerDensity (ImGui 1.91+):
//      图集按 density 倍率栅格化, 渲染时再按字号缩小取样。
//      这样一套 31px 图集在 12~40px 的所有字号下都清晰,
//      不必为每个字号单独 AddFontFromMemoryTTF(那会把内存吃爆)。
//   4. 接入系统字体档: 雅黑 / 宋体 / 楷体, 路径照搬查理王者的候选列表。
//      机型没有对应字体时自动跳过, 只在真正加载成功时才出现在档位里。
//   5. 接入苹方 (IOS_PingFang.ttf), 打包进 assets 作为内置档。
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

bool  g_loaded  = false;
int   g_current = FONT_CAESAR;
char  g_path[256] = "";

// 外部字体候选: 优先级最高, 方便用户不改代码就换字体
const char* kExternal[] = {
    "/sdcard/Charlie/font.ttf",
    "/sdcard/Charlie/zh.ttf",
    nullptr
};

// ============================================================
// 基准字号
//
// 【为什么是 31】
//   参考工程 draw.cpp:1069 用的是
//       AddFontFromMemoryTTF(font_v, font_v_size, 31.0f, ...)
//   31px 是它整套 UI 的排版基准。我们跟它对齐, 中文在 3x 屏上
//   笔画展开到 2~3 物理像素, 抗锯齿后边缘才立得住。
//
// 【为什么还要 RasterizerDensity】
//   我们所有控件字号是按 metrics.s 缩放的 (m.s ≈ 1.0~1.55),
//   实际渲染字号会在 22~48px 之间浮动。如果图集只在 31px 栅格化,
//   渲染成 48px 时就是单纯放大位图 -> 发虚。
//   DENSITY 让图集按 31*2 = 62px 栅格化, 渲染时缩小取样,
//   于是所有字号都清晰。代价是图集内存 x4, 一次性开销。
// ============================================================
constexpr float kSizePx  = 31.f;
constexpr float kDensity = 2.0f;    // 栅格化倍率

bool FileReadable(const char* p) {
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

struct FontSrc {
    const unsigned char* begin;
    const unsigned char* end;
    const char* label;
};

// 内置字体源: 编译期打包进 .rodata
FontSrc SourceOf(int id) {
    switch (id) {
    case FONT_CAESAR:
        return { _binary_caesar_ttf_start, _binary_caesar_ttf_end, "凯撒金" };
    case FONT_NOTO:
        return { _binary_font_ttf_start, _binary_font_ttf_end, "思源黑" };
    case FONT_PINGFANG:
        return { _binary_pingfang_ttf_start, _binary_pingfang_ttf_end, "苹方" };
    default:
        return { nullptr, nullptr, "内置默认" };
    }
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
    // 调太高(>1.15)会让字重看起来像假粗体。
    cfg.RasterizerMultiply = 1.08f;
    // 关键: 按 2x 栅格化, 渲染时缩小取样 -> 所有字号都清晰
    cfg.RasterizerDensity  = kDensity;
    return cfg;
}

// ============================================================
// 系统字体
//
// 路径候选列表照搬 查理王者/jni/src/Android_draw/draw.cpp:988 LoadUiSysFonts。
// 每类给多个候选: 不同 ROM 装的字体文件名不同 (小米 MiSans /
// 类原生 Noto / 老机器 DroidSansFallback), 按顺序试第一个能打开的。
//
// 【为什么不写死一个】
//   写死 /system/fonts/NotoSansCJK-Regular.ttc, 在小米/OPPO 上直接 404,
//   整个字体档位会静默失效, 用户以为功能坏了。
// ============================================================
struct SysFontSlot {
    const char* label;
    const char* paths[5];   // nullptr 结尾
};

const SysFontSlot kSysFonts[] = {
    { "雅黑", { "/system/fonts/MiSans-Regular.ttf",
                "/system/fonts/NotoSansCJK-Regular.ttc",
                "/system/fonts/NotoSansCJK-Regular.ttf",
                "/system/fonts/DroidSansFallback.ttf",
                nullptr } },
    { "宋体", { "/system/fonts/NotoSerifCJK-Regular.ttc",
                "/system/fonts/NotoSerifCJK-Regular.ttf",
                "/system/fonts/NotoSerifSC-Regular.otf",
                nullptr } },
    { "楷体", { "/system/fonts/NotoSerifCJK-Regular.ttc",
                "/system/fonts/DroidSansFallback.ttf",
                nullptr } },
};

constexpr int kSysCount = (int)(sizeof(kSysFonts) / sizeof(kSysFonts[0]));

// 系统字体加载结果: nullptr 表示该机型没有
ImFont* g_sysFont[kSysCount] = { nullptr, nullptr, nullptr };
char    g_sysPath[kSysCount][192] = {};

} // namespace

// ============================================================
// 对外接口
// ============================================================

const char* DisplayName(int id) {
    switch (id) {
    case FONT_CAESAR:   return "凯撒金";
    case FONT_NOTO:     return "思源黑";
    case FONT_PINGFANG: return "苹方";
    case FONT_SYS_YAHEI:return "雅黑";
    case FONT_SYS_SONG: return "宋体";
    case FONT_SYS_KAI:  return "楷体";
    case FONT_DEFAULT:  return "内置默认";
    default:            return "未知";
    }
}

bool LoadWith(int id) {
    if (id < 0 || id >= FONT_COUNT) id = FONT_CAESAR;

    ImGuiIO& io = ImGui::GetIO();
    // 全字库: 覆盖 CJK 统一表意区 + 扩展 A, 不是只有 2500 常用字。
    // 图集纹理会变大 (约 4096x4096), 但 31px + density 2 本来就需要
    // 这个尺寸, 属于必要开销。
    const ImWchar* rng = io.Fonts->GetGlyphRangesChineseFull();

    // ---------- 外部字体优先 ----------
    for (int i = 0; kExternal[i]; ++i) {
        if (!FileReadable(kExternal[i])) continue;
        ImFontConfig cfg = MakeConfig();
        ImFont* f = io.Fonts->AddFontFromFileTTF(kExternal[i], kSizePx, &cfg, rng);
        if (f) {
            snprintf(g_path, sizeof(g_path), "外部 %s", kExternal[i]);
            g_current = id;
            g_loaded  = true;
            printf("[Charlie] font: %s (%.0fpx, density %.1f, CJK-Full)\n",
                   g_path, kSizePx, kDensity);
            return true;
        }
    }

    // ---------- 内置默认 (无中文) ----------
    if (id == FONT_DEFAULT) {
        io.Fonts->AddFontDefault();
        snprintf(g_path, sizeof(g_path), "内置默认");
        g_current = id;
        g_loaded  = true;
        printf("[Charlie] font: 内置默认 (无中文)\n");
        return true;
    }

    // ---------- 系统字体档 ----------
    if (id >= FONT_SYS_YAHEI && id <= FONT_SYS_KAI) {
        const int si = id - FONT_SYS_YAHEI;
        // 首次使用某个系统档时才真正加载 (lazy),
        // 避免把三套 CJK 全字库同时塞进图集 (每套约 60MB 纹理)。
        if (!g_sysFont[si]) {
            const SysFontSlot& slot = kSysFonts[si];
            for (int p = 0; slot.paths[p]; ++p) {
                if (!FileReadable(slot.paths[p])) continue;
                ImFontConfig cfg = MakeConfig();
                ImFont* f = io.Fonts->AddFontFromFileTTF(slot.paths[p], kSizePx, &cfg, rng);
                if (f) {
                    g_sysFont[si] = f;
                    snprintf(g_sysPath[si], sizeof(g_sysPath[si]), "%s", slot.paths[p]);
                    break;
                }
            }
            if (!g_sysFont[si]) {
                // 该机型没有这套字体: 回退到凯撒金, 并明确告知
                printf("[Charlie] font: %s 不可用, 回退凯撒金\n", slot.label);
                return LoadWith(FONT_CAESAR);
            }
        }
        snprintf(g_path, sizeof(g_path), "系统 %s (%s)", kSysFonts[si].label, g_sysPath[si]);
        g_current = id;
        g_loaded  = true;
        printf("[Charlie] font: %s\n", g_path);
        return true;
    }

    // ---------- 内置中文 ----------
    FontSrc src = SourceOf(id);
    const size_t size = (size_t)(src.end - src.begin);
    if (size > 1024) {
        ImFontConfig cfg = MakeConfig();
        cfg.FontDataOwnedByAtlas = false;   // 数据在 .rodata, 不能释放

        ImFont* f = io.Fonts->AddFontFromMemoryTTF(
            (void*)src.begin, (int)size, kSizePx, &cfg, rng);

        if (f) {
            snprintf(g_path, sizeof(g_path), "%s (%zu 字节)", src.label, size);
            g_current = id;
            g_loaded  = true;
            printf("[Charlie] font: %s (%.0fpx, density %.1f, CJK-Full)\n",
                   g_path, kSizePx, kDensity);
            return true;
        }
        printf("[Charlie] font: %s 解析失败\n", src.label);
    }

    // ---------- 兜底 ----------
    io.Fonts->AddFontDefault();
    snprintf(g_path, sizeof(g_path), "回退默认");
    g_current = id;
    g_loaded  = true;
    printf("[Charlie] font: 全部失败, 回退 ProggyClean (中文显示为方块)\n");
    return false;
}

bool Load() {
    if (g_loaded) return true;
    return LoadWith(g_current);
}

bool Loaded() { return g_loaded; }
int  CurrentId() { return g_current; }
const char* SourcePath() { return g_path; }

bool SysAvailable(int id) {
    const int si = id - FONT_SYS_YAHEI;
    if (si < 0 || si >= kSysCount) return false;
    // 只看文件是否可读, 不做真正的字体解析 (解析很贵)
    const SysFontSlot& slot = kSysFonts[si];
    for (int p = 0; slot.paths[p]; ++p)
        if (FileReadable(slot.paths[p])) return true;
    return false;
}

// ImGui 上下文被重建后 (例如屏幕旋转重建渲染栈), 字体随上下文消失,
// 必须清掉加载标志, 否则下次 Load() 会误判"已加载"而跳过。
//
// 【系统档的指针也必须一起清】
//   这些 ImFont* 都指向旧上下文 FontAtlas 里的对象, 上下文销毁后全部悬空。
//   不清就会在下次 SetFont 时崩溃 (野指针), 而且是随机崩, 很难查。
void Reset() {
    g_loaded = false;
    g_path[0] = 0;
    for (int i = 0; i < kSysCount; ++i) {
        g_sysFont[i] = nullptr;
        g_sysPath[i][0] = 0;
    }
}

} // namespace fonts
} // namespace charlie
