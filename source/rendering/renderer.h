#pragma once

#include "core/defines.h"
#include "camera.h"

#if HE_OS_WINDOWS
#define HE_RHI_VULKAN
#endif

enum RenderingAPI
{
    RenderingAPI_Vulkan
};

struct Renderer_State
{
    U32 back_buffer_width;
    U32 back_buffer_height;

    Camera camera;
    FPS_Camera_Controller camera_controller;
};

struct Renderer
{
    bool (*init)(struct Renderer_State *renderer_state,
                 struct Engine *engine,
                 struct Memory_Arena *arena);

    void (*deinit)(struct Renderer_State *renderer_state);

    void (*on_resize)(struct Renderer_State *renderer_state, U32 width, U32 height);

    void (*draw)(struct Renderer_State *renderer_state, F32 delta_time);
};

bool request_renderer(RenderingAPI rendering_api, Renderer *renderer);