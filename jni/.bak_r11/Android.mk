LOCAL_PATH := $(call my-dir)

# ============================================================
# freetype 静态库 (仅 arm64-v8a)
# ============================================================
include $(CLEAR_VARS)
LOCAL_MODULE := charlie_freetype
LOCAL_SRC_FILES := lib/arm64-v8a/libfreetype.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/ImGui/misc/freetype
include $(PREBUILT_STATIC_LIBRARY)

# ============================================================
# 内置中文字体 (DroidSansFallback.ttf)
#
# 用 objcopy 把 ttf 包成 .o (符号 _binary_font_ttf_start/_end),
# 直接链进可执行文件 —— 运行时不依赖设备字体, 排除乱码。
#
# 生成命令 (已在仓内执行过, 产物 assets/font.o 已存在):
#   llvm-objcopy -I binary -O elf64-littleaarch64 \
#     --rename-section .data=.rodata,alloc,load,readonly,data,contents \
#     font.ttf font.o
# 若替换字体, 在 jni/assets 目录下重新执行上面命令即可。
# ============================================================
# 内置字体: 凯撒金 (2MB) + 思源黑 (8MB)
include $(CLEAR_VARS)
LOCAL_MODULE := charlie_font
LOCAL_SRC_FILES := assets/font.o
include $(PREBUILT_OBJECT)

include $(CLEAR_VARS)
LOCAL_MODULE := charlie_font_caesar
LOCAL_SRC_FILES := assets/caesar.o
include $(PREBUILT_OBJECT)

# 苹方 (IOS_PingFang, 887KB) —— 系统字体档缺失时的中文回退,
# 也是三套内置字体里笔画最细最接近 iOS 观感的一套。
include $(CLEAR_VARS)
LOCAL_MODULE := charlie_font_pingfang
LOCAL_SRC_FILES := assets/pingfang.o
include $(PREBUILT_OBJECT)

# ============================================================
# 主模块
# ============================================================
include $(CLEAR_VARS)

LOCAL_MODULE := charlie_ui

LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden
LOCAL_CPPFLAGS += -O2 -g -gdwarf-4
LOCAL_CPPFLAGS += -fexceptions -fpermissive
LOCAL_CPPFLAGS += -Wno-error=format-security -Wno-error=c++11-narrowing
LOCAL_CPPFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS
LOCAL_CPPFLAGS += -DIMGUI_ENABLE_FREETYPE
LOCAL_CPPFLAGS += -DIMGUI_IMPL_OPENGL_ES3

LOCAL_CFLAGS := -O2 -g -w

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/Tools
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/backends
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include/ImGui/misc/freetype
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/ui
LOCAL_C_INCLUDES += $(LOCAL_PATH)/src/platform

# ---------- 源文件 ----------
LOCAL_SRC_FILES := \
    src/main.cpp \
    src/ImGui/imgui.cpp \
    src/ImGui/imgui_draw.cpp \
    src/ImGui/imgui_tables.cpp \
    src/ImGui/imgui_widgets.cpp \
    src/ImGui/backends/imgui_impl_opengl3.cpp \
    src/ImGui/backends/imgui_impl_android.cpp \
    src/ImGui/misc/freetype/imgui_freetype.cpp \
    src/Tools/AndroidImgui.cpp \
    src/Tools/GraphicsManager.cpp \
    src/Tools/OpenGLGraphics.cpp \
    src/Tools/my_imgui_impl_android.cpp \
    src/Tools/stb_image.cpp \
    src/ui/CharlieTheme.cpp \
    src/ui/CharlieAnim.cpp \
    src/ui/CharlieWidgets.cpp \
    src/ui/CharlieIsland.cpp \
    src/ui/CharlieFreeFps.cpp \
    src/ui/CharlieState.cpp \
    src/ui/CharliePages.cpp \
    src/ui/CharlieShell.cpp \
    src/ui/CharlieFonts.cpp \
    src/platform/CharlieVolume.cpp \
    src/platform/CharlieTouch.cpp \
    src/platform/CharlieDisplay.cpp \
    src/platform/CharlieSingle.cpp

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -lz -lm -ldl

LOCAL_STATIC_LIBRARIES := charlie_freetype
# 内置字体目标文件 (不能放 LOCAL_SRC_FILES, .o 会被当源文件编译)
LOCAL_WHOLE_STATIC_LIBRARIES :=
LOCAL_LDFLAGS += $(LOCAL_PATH)/assets/font.o
LOCAL_LDFLAGS += $(LOCAL_PATH)/assets/caesar.o
LOCAL_LDFLAGS += $(LOCAL_PATH)/assets/pingfang.o

LOCAL_STRIP_MODULE := false

include $(BUILD_EXECUTABLE)
