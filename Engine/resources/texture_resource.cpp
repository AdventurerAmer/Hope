#include "resources/texture_resource.h"
#include "resources/resource_system.h"
#include "core/platform.h"
#include "core/memory.h"
#include "core/file_system.h"
#include "rendering/renderer.h"

#include <stb/stb_image.h>

bool condition_texture_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    S32 width;
    S32 height;
    S32 channels;

    stbi_uc *pixels = stbi_load_from_memory(asset_file_result->data, u64_to_u32(asset_file_result->size), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        return false;
    }

    HE_DEFER { stbi_image_free(pixels); };

    U64 offset = 0;
    U8 *buffer = &arena->base[arena->offset];

    Resource_Header header = make_resource_header(Asset_Type::TEXTURE, resource->asset_uuid, resource->uuid);
    copy_memory(&buffer[offset], &header, sizeof(header));
    offset += sizeof(header);

    Texture_Resource_Info texture_resource_info =
    {
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_UNORM,
        .mipmapping = true,
        .data_offset = sizeof(Resource_Header) + sizeof(Texture_Resource_Info)
    };

    copy_memory(&buffer[offset], &texture_resource_info, sizeof(Texture_Resource_Info));
    offset += sizeof(Texture_Resource_Info);

    copy_memory(&buffer[offset], pixels, width * height * sizeof(U32)); // todo(amer): @Hardcoding
    offset += width * height * sizeof(U32);

    bool result = write_entire_file(resource->absolute_path, buffer, offset);
    return result;
}

bool load_texture_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    Texture_Resource_Info info;
    platform_read_data_from_file(open_file_result, sizeof(Resource_Header), &info, sizeof(Texture_Resource_Info));

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    U64 data_size = sizeof(U32) * info.width * info.height;
    U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, info.width * info.height);
    bool success = platform_read_data_from_file(open_file_result, info.data_offset, data, data_size);

    void *data_array[] = { data };

    Texture_Descriptor texture_descriptor =
    {
        .width = info.width,
        .height = info.height,
        .format = info.format,
        .data_array = to_array_view(data_array),
        .mipmapping = info.mipmapping,
        .sample_count = 1
    };

    Texture_Handle texture_handle = renderer_create_texture(texture_descriptor);
    resource->index = texture_handle.index;
    resource->generation = texture_handle.generation;
    return success;
}

void unload_texture_resource(Resource *resource)
{
    HE_ASSERT(resource->state != Resource_State::UNLOADED);

    Render_Context render_context = get_render_context();

    Texture_Handle texture_handle = { resource->index, resource->generation };

    if (is_valid_handle(&render_context.renderer_state->textures, texture_handle) &&
        (texture_handle != render_context.renderer_state->white_pixel_texture || texture_handle != render_context.renderer_state->normal_pixel_texture))
    {
        renderer_destroy_texture(texture_handle);
        resource->index = render_context.renderer_state->white_pixel_texture.index;
        resource->generation = render_context.renderer_state->white_pixel_texture.generation;
    }
}