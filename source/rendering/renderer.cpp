#include "rendering/renderer.h"
#include "rendering/vulkan.h"

bool request_renderer(RenderingAPI rendering_api,
                      Renderer *renderer)
{
    bool result = true;

    // todo(amer): check if rendering_api supported by the platform.
    switch (rendering_api)
    {
        case RenderingAPI_Vulkan:
        {
            renderer->init = &vulkan_renderer_init;
            renderer->deinit = &vulkan_renderer_deinit;
            renderer->on_resize = &vulkan_renderer_on_resize;
            renderer->draw = &vulkan_renderer_draw;
        } break;

        default:
        {
            result = false;
        } break;
    }

    return result;
}