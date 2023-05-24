#include "rendering/renderer.h"
#include "rendering/vulkan/vulkan.h"

bool request_renderer(RenderingAPI rendering_api,
                      Renderer *renderer)
{
    bool result = true;

    switch (rendering_api)
    {
        case RenderingAPI_Vulkan:
        {
#ifdef HE_RHI_VULKAN
            renderer->init = &vulkan_renderer_init;
            renderer->deinit = &vulkan_renderer_deinit;
            renderer->on_resize = &vulkan_renderer_on_resize;
            renderer->draw = &vulkan_renderer_draw;
#else
            result = false;
#endif
        } break;

        default:
        {
            result = false;
        } break;
    }

    return result;
}