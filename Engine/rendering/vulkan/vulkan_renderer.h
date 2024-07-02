#pragma once

#include "vulkan_types.h"
#include <imgui.h>

bool vulkan_renderer_init(struct Engine *engine, struct Renderer_State *renderer_state);
void vulkan_renderer_deinit();

void vulkan_renderer_wait_for_gpu_to_finish_all_work();

void vulkan_renderer_on_resize(U32 width, U32 height);

bool vulkan_renderer_create_texture(Texture_Handle texture_handle, const Texture_Descriptor &descriptor, Upload_Request_Handle upload_request_handle);
void vulkan_renderer_destroy_texture(Texture_Handle texture_handle, bool immediate);

bool vulkan_renderer_create_sampler(Sampler_Handle sampler_handle, const Sampler_Descriptor &descriptor);
void vulkan_renderer_destroy_sampler(Sampler_Handle sampler_handle, bool immediate);

bool vulkan_renderer_create_buffer(Buffer_Handle buffer_handle, const Buffer_Descriptor &descriptor);
void vulkan_renderer_destroy_buffer(Buffer_Handle buffer_handle, bool immediate);

bool vulkan_renderer_create_shader(Shader_Handle shader_handle, const Shader_Descriptor &descriptor);
void vulkan_renderer_destroy_shader(Shader_Handle shader_handle, bool immediate);

bool vulkan_renderer_create_pipeline_state(Pipeline_State_Handle pipeline_state_handle, const Pipeline_State_Descriptor &descriptor);
void vulkan_renderer_destroy_pipeline_state(Pipeline_State_Handle pipeline_state_handle, bool immediate);

void vulkan_renderer_update_bind_group(Bind_Group_Handle bind_group_handle, const Array_View< Update_Binding_Descriptor > &update_binding_descriptors);
void vulkan_renderer_set_bind_groups(U32 first_bind_group, const Array_View< Bind_Group_Handle > &bind_group_handles);

bool vulkan_renderer_create_render_pass(Render_Pass_Handle render_pass_handle, const Render_Pass_Descriptor &descriptor);
void vulkan_renderer_begin_render_pass(Render_Pass_Handle render_pass_handle, Frame_Buffer_Handle frame_buffer_handle, const Array_View< Clear_Value > &clear_values);
void vulkan_renderer_end_render_pass(Render_Pass_Handle render_pass_handle);
void vulkan_renderer_destroy_render_pass(Render_Pass_Handle render_pass_handle, bool immediate);

bool vulkan_renderer_create_frame_buffer(Frame_Buffer_Handle frame_buffer_handle, const Frame_Buffer_Descriptor &descriptor);
void vulkan_renderer_destroy_frame_buffer(Frame_Buffer_Handle frame_buffer_handle, bool immediate);

bool vulkan_renderer_create_static_mesh(Static_Mesh_Handle static_mesh_handle, const Static_Mesh_Descriptor &descriptor, Upload_Request_Handle upload_request_handle);

bool vulkan_renderer_create_semaphore(Semaphore_Handle semaphore_handle, const Renderer_Semaphore_Descriptor &descriptor);
U64 vulkan_renderer_get_semaphore_value(Semaphore_Handle semaphore_handle);
void vulkan_renderer_destroy_semaphore(Semaphore_Handle semaphore_handle);

void vulkan_renderer_destroy_upload_request(Upload_Request_Handle upload_request_handle);

void vulkan_renderer_set_vsync(bool enabled);

void vulkan_renderer_begin_frame();

void vulkan_renderer_set_viewport(U32 width, U32 height);

void vulkan_renderer_set_vertex_buffers(const Array_View< Buffer_Handle > &vertex_buffer_handles, const Array_View< U64 > &offsets);
void vulkan_renderer_set_index_buffer(Buffer_Handle index_buffer_handle, U64 offset);

void vulkan_renderer_set_pipeline_state(Pipeline_State_Handle pipeline_state_handle);
void vulkan_renderer_draw_sub_mesh(Static_Mesh_Handle static_mesh_handle, U32 first_instance, U32 sub_mesh_index);
void vulkan_renderer_draw_fullscreen_triangle();

void vulkan_renderer_fill_buffer(Buffer_Handle buffer_handle, U32 value);

void vulkan_renderer_clear_texture(Texture_Handle texture_handle, Clear_Value clear_value);
void vulkan_renderer_change_texture_state(Texture_Handle texture_handle, Resource_State resource_state);

void vulkan_renderer_invalidate_buffer(Buffer_Handle buffer_handle);

void vulkan_renderer_end_frame();

bool vulkan_renderer_init_imgui();
void vulkan_renderer_imgui_new_frame();
void vulkan_renderer_imgui_add_texture(Texture_Handle texture);
ImTextureID vulkan_renderer_imgui_get_texture_id(Texture_Handle texture);
void vulkan_renderer_imgui_render();

void vulkan_renderer_destroy_resources_at_frame(U32 frame_index);

struct Enviornment_Map_Render_Data
{
    F32 *hdr_data;
    U32 hdr_width;
    U32 hdr_height;

    Buffer_Handle globals_uniform_buffer;

    Texture_Handle hdr_cubemap_handle;
    Texture_Handle irradiance_cubemap_handle;
    Texture_Handle prefilter_cubemap_handle;
};

void vulkan_renderer_fill_brdf_lut(Texture_Handle brdf_lut_texture_handle);

void vulkan_renderer_hdr_to_environment_map(const Enviornment_Map_Render_Data &render_data);

Memory_Requirements vulkan_renderer_get_texture_memory_requirements(const Texture_Descriptor &descriptor);