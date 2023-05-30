#pragma once

#include "vulkan_types.h"

S32 find_memory_type_index(Vulkan_Context *context,
                           VkMemoryRequirements memory_requirements,
                           VkMemoryPropertyFlags memory_property_flags);

bool vulkan_renderer_init(struct Renderer_State *renderer_state,
                          struct Engine *engine,
                          struct Memory_Arena *arena);

void vulkan_renderer_deinit(struct Renderer_State *renderer_state);

void vulkan_renderer_wait_for_gpu_to_finish_all_work(struct Renderer_State *renderer_state);

void vulkan_renderer_on_resize(struct Renderer_State *renderer_state, U32 width, U32 height);

bool vulkan_renderer_create_texture(Texture *texture, U32 width, U32 height,
                                    void *data, TextureFormat format, bool mipmapping);

void vulkan_renderer_destroy_texture(Texture *texture);

bool vulkan_renderer_create_static_mesh(Static_Mesh *static_mesh, void *vertices, U32 vertex_count, U16 *indices, U32 index_count);
void vulkan_renderer_destroy_static_mesh(Static_Mesh *static_mesh);

void vulkan_renderer_begin_frame(struct Renderer_State *renderer_state, const struct Scene_Data *scene_data);
void vulkan_renderer_submit_static_mesh(struct Renderer_State *renderer_state, struct Static_Mesh *static_mesh, U32 model_count, const glm::mat4 *models);
void vulkan_renderer_end_frame(struct Renderer_State *renderer_state);