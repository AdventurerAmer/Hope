#include "engine.h"
#include "platform.h"
#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"
#include "logging.h"
#include "cvars.h"
#include "job_system.h"
#include "file_system.h"

// #include "resources/resource_system.h"
#include "assets/asset_manager.h"

#include <chrono>
#include <imgui.h>

#include <stb/stb_image.h>

bool hope_app_init(Engine *engine);
void hope_app_on_event(Engine *engine, Event event);
void hope_app_on_update(Engine *engine, F32 delta_time);
void hope_app_shutdown(Engine *engine);

bool startup(Engine *engine)
{
    bool inited = init_memory_system();
    if (!inited)
    {
        return false;
    }

    init_logging_system();
    
    init_cvars(HE_STRING_LITERAL("config.cvars"));
    
    engine->show_cursor = false;
    engine->lock_cursor = false;

    engine->name = HE_STRING_LITERAL("Hope");
    engine->app_name = HE_STRING_LITERAL("Hope");

    String &engine_name = engine->name;
    String &app_name = engine->app_name;
    
    Window *window = &engine->window;
    window->width = 1296;
    window->height = 759;
    window->mode = Window_Mode::WINDOWED;

    U32 &window_width = window->width;
    U32 &window_height = window->height;
    U8 &window_mode = (U8 &)window->mode;
    
    HE_DECLARE_CVAR("platform", engine_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", app_name, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_width, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_height, CVarFlag_None);
    HE_DECLARE_CVAR("platform", window_mode, CVarFlag_None);

    bool window_created = platform_create_window(window, app_name.data, (U32)window_width, (U32)window_height, (Window_Mode)window_mode);
    if (!window_created)
    {
        HE_LOG(Core, Fetal, "failed to create window\n");
        return false;
    }

    bool input_inited = init_input(&engine->input);
    if (!input_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize input system\n");
        return false;
    }

    bool job_system_inited = init_job_system();
    if (!job_system_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize job system\n");
        return false;
    }

    bool renderer_state_inited = init_renderer_state(engine);
    if (!renderer_state_inited)
    {
        HE_LOG(Core, Fetal, "failed to initialize render system\n");
        return false;
    }

    bool asset_manager_inited = init_asset_manager(HE_STRING_LITERAL("assets"));

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;
    Renderer *renderer = render_context.renderer;
    Scene_Data *scene_data = &renderer_state->scene_data;

    scene_data->directional_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->directional_light.color = { 1.0f, 1.0f, 1.0f };
    scene_data->directional_light.intensity = 1.0f;

    scene_data->point_light.position = { -3.0f, 2.0f, 0.0f };
    scene_data->point_light.radius = 5.0f;
    scene_data->point_light.color = { 1.0f, 1.0f, 1.0f };
    scene_data->point_light.intensity = 1.0f;

    scene_data->spot_light.position = { 3.0f, 2.0f, 0.0f };
    scene_data->spot_light.direction = { 0.0f, -1.0f, 0.0f };
    scene_data->spot_light.outer_angle = 45.0f;
    scene_data->spot_light.inner_angle = 30.0f;
    scene_data->spot_light.radius = 5.0f;
    scene_data->spot_light.color = { 1.0f, 1.0f, 1.0f };
    scene_data->spot_light.intensity = 1.0f;

    bool app_inited = hope_app_init(engine);
    return app_inited;
}

void on_event(Engine *engine, Event event)
{
    switch (event.type)
    {
        case Event_Type::RESIZE:
        {
            Window *window = &engine->window;
            window->width = event.window_width;
            window->height = event.window_height;
            renderer_on_resize(event.client_width, event.client_height);
        } break;
    }

    hope_app_on_event(engine, event);
}

static U8* get_pointer(Shader_Struct *_struct, U8 *data, String name)
{
    for (U32 i = 0; i < _struct->member_count; i++)
    {
        Shader_Struct_Member *member = &_struct->members[i];
        if (member->name == name)
        {
            return &data[member->offset];
        }
    }

    return nullptr;
}

void game_loop(Engine *engine, F32 delta_time)
{
    Render_Context render_context = get_render_context();
    Renderer *renderer = render_context.renderer;
    Renderer_State *renderer_state = render_context.renderer_state;
    
    renderer_handle_upload_requests();

    if (!engine->is_minimized)
    {
        imgui_new_frame();
    }

    Temprary_Memory_Arena scratch_memory = begin_scratch_memory();
    hope_app_on_update(engine, delta_time);

    if (!engine->is_minimized)
    {   
        U32 frame_index = renderer_state->current_frame_in_flight_index;
        Buffer *object_data_storage_buffer = get(&renderer_state->buffers, renderer_state->object_data_storage_buffers[frame_index]);
        renderer_state->object_data_base = (Shader_Object_Data *)object_data_storage_buffer->data;
        renderer_state->object_data_count = 0;
        renderer_state->current_pipeline_state_handle = Resource_Pool< Pipeline_State >::invalid_handle;

        renderer->begin_frame();

        Buffer *global_uniform_buffer = get(&renderer_state->buffers, renderer_state->globals_uniform_buffers[frame_index]);
        
        Shader_Struct *globals_struct = renderer_find_shader_struct(renderer_state->default_shader, HE_STRING_LITERAL("Globals"));
        glm::mat4 *view = (glm::mat4 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("view"));
        glm::mat4 *projection = (glm::mat4 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("projection"));
        glm::vec3 *eye = (glm::vec3 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("eye"));
        glm::vec3 *directional_light_direction = (glm::vec3 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("directional_light_direction"));
        glm::vec3 *directional_light_color = (glm::vec3 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("directional_light_color"));
        U32 *light_count = (U32 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("light_count")); 
        Shader_Light *lights = (Shader_Light *)get_pointer(globals_struct, (U8*)global_uniform_buffer->data, HE_STRING_LITERAL("lights"));
        F32 *gamma = (F32 *)get_pointer(globals_struct, (U8 *)global_uniform_buffer->data, HE_STRING_LITERAL("gamma"));

        Scene_Data *scene_data = &renderer_state->scene_data;

        *view = scene_data->view;
        scene_data->projection[1][1] *= -1;
        *projection = scene_data->projection;
        *eye = scene_data->eye;
        *directional_light_direction = scene_data->directional_light.direction;
        *directional_light_color = srgb_to_linear(scene_data->directional_light.color, renderer_state->gamma) * scene_data->directional_light.intensity;
        *light_count = 2;
        *gamma = renderer_state->gamma;

        {
            Shader_Light *light = &lights[0];
            light->position = scene_data->point_light.position;
            light->radius = scene_data->point_light.radius;
            
            light->direction = { 0.0f, 0.0f, 0.0f };
            light->outer_angle = glm::radians(180.0f);
            light->inner_angle = glm::radians(0.0f);

            light->color = srgb_to_linear(scene_data->point_light.color, renderer_state->gamma) * scene_data->point_light.intensity;
        }

        {
            Shader_Light *light = &lights[1];
            light->position = scene_data->spot_light.position;
            light->radius = scene_data->spot_light.radius;
            light->direction = scene_data->spot_light.direction;
            light->outer_angle = glm::radians(scene_data->spot_light.outer_angle);
            light->inner_angle = glm::radians(scene_data->spot_light.inner_angle);
            light->color = srgb_to_linear(scene_data->spot_light.color, renderer_state->gamma) * scene_data->spot_light.intensity;
        }

        U32 texture_count = renderer_state->textures.capacity;
        Texture_Handle *textures = HE_ALLOCATE_ARRAY(scratch_memory.arena, Texture_Handle, texture_count);
        Sampler_Handle *samplers = HE_ALLOCATE_ARRAY(scratch_memory.arena, Sampler_Handle, texture_count);

        for (auto it = iterator(&renderer_state->textures); next(&renderer_state->textures, it);)
        {
            Texture *texture = get(&renderer_state->textures, it);

            if (texture->is_attachment || !texture->is_uploaded_to_gpu)
            {
                textures[it.index] = renderer_state->white_pixel_texture;
            }
            else
            {
                textures[it.index] = it;
            }

            samplers[it.index] = texture->is_cubemap ? renderer_state->default_cubemap_sampler : renderer_state->default_texture_sampler;
        }
        
        Update_Binding_Descriptor globals_uniform_buffer_binding =
        {
            .binding_number = 0,
            .element_index = 0,
            .count = 1,
            .buffers = &renderer_state->globals_uniform_buffers[frame_index]
        };
        
        Update_Binding_Descriptor object_data_storage_buffer_binding =
        {
            .binding_number = 1,
            .element_index = 0,
            .count = 1,
            .buffers = &renderer_state->object_data_storage_buffers[frame_index]
        };

        Update_Binding_Descriptor update_binding_descriptors[] =
        {
            globals_uniform_buffer_binding,
            object_data_storage_buffer_binding
        };
        renderer_update_bind_group(renderer_state->per_frame_bind_groups[frame_index], to_array_view(update_binding_descriptors));

        Update_Binding_Descriptor update_textures_binding_descriptors[] =
        {
            {
                .binding_number = 0,
                .element_index = 0,
                .count = texture_count,
                .textures = textures,
                .samplers = samplers
            },
        };
        renderer_update_bind_group(renderer_state->per_render_pass_bind_groups[frame_index], to_array_view(update_textures_binding_descriptors));

        Bind_Group_Handle bind_groups[] =
        {
            renderer_state->per_frame_bind_groups[frame_index],
            renderer_state->per_render_pass_bind_groups[frame_index]
        };

        renderer->set_bind_groups(0, to_array_view(bind_groups));

        render(&renderer_state->render_graph, renderer, renderer_state);

        renderer->end_frame();
        
        renderer_state->current_frame_in_flight_index++;
        if (renderer_state->current_frame_in_flight_index >= renderer_state->frames_in_flight)
        {
            renderer_state->current_frame_in_flight_index = 0;
        }
    }

    end_temprary_memory(&scratch_memory);
}

void shutdown(Engine *engine)
{
    hope_app_shutdown(engine);

    deinit_asset_manager();

    deinit_renderer_state();

    deinit_job_system();

    deinit_cvars();

    deinit_logging_system();

    deinit_memory_system();
}