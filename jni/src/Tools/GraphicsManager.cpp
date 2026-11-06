//
// Charlie UI · 图形后端工厂
// 只保留 OpenGL ES 3 后端。Vulkan 后端在部分设备/驱动上由
// libvulkan 运行时解析, 不是所有机型都可用, 而本模板追求的
// 是"拷进去就能跑", 因此默认只走 OpenGL。
// 如需 Vulkan: 在 Android.mk 里补上 VulkanGraphics.cpp 与
// vulkan_wrapper.cpp, 并在下面恢复对应分支。
//

#include "GraphicsManager.h"
#include "OpenGLGraphics.h"

std::unique_ptr<AndroidImgui> GraphicsManager::getGraphicsInterface(GraphicsAPI api) {
    switch (api) {
        case OPENGL: {
            return std::make_unique<OpenGLGraphics>();
        }
        case VULKAN:
        default: {
            // 未接入 Vulkan 后端时回退到 OpenGL, 保证程序永远有渲染器
            return std::make_unique<OpenGLGraphics>();
        }
    }
}
