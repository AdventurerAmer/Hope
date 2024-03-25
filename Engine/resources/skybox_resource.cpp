#include "resources/skybox_resource.h"
#include "resources/resource_system.h"
#include "resources/texture_resource.h"

#include "core/platform.h"
#include "core/memory.h"
#include "core/file_system.h"

#include "containers/string.h"

#include "rendering/renderer.h"

bool condition_skybox_to_resource(Read_Entire_File_Result *asset_file_result, Asset *asset, Resource *resource, Memory_Arena *arena)
{
    reset(&resource->resource_refs);

    String str = { .count = asset_file_result->size, .data = (const char *)asset_file_result->data };
    String white_space = HE_STRING_LITERAL(" \n\t\r\v\f");
    str = eat_chars(str, white_space);

    String version_lit = HE_STRING_LITERAL("version");
    if (!starts_with(str, version_lit))
    {
        return false;
    }

    str = advance(str, version_lit.count);
    str = eat_chars(str, white_space);
    S64 index = find_first_char_from_left(str, white_space);
    if (index == -1)
    {
        return false;
    }

    String version_value = sub_string(str, 0, index);
    U32 version = u64_to_u32(str_to_u64(version_value));
    str = advance(str, version_value.count);
    str = eat_chars(str, white_space);

    String tint_color_lit = HE_STRING_LITERAL("tint_color");
    if (!starts_with(str, tint_color_lit))
    {
        return false;
    }
    str = advance(str, tint_color_lit.count);
    str = eat_chars(str, white_space);

    auto parse_vector3 = [&](String &s) -> glm::vec3
    {
        glm::vec3 result = { 0.0f, 0.0f, 0.0f };

        for (int i = 0; i < 3; i++)
        {
            S64 index = find_first_char_from_left(s, white_space);
            String value_lit = sub_string(s, 0, index);
            result[i] = str_to_f32(value_lit);
            s = advance(s, index + 1);
        }

        return result;
    };

    Skybox_Resource_Info skybox_resource_info = {};
    skybox_resource_info.tint_color = parse_vector3(str);
    str = eat_chars(str, white_space);

    U64 texture_assets[6] = {};

    for (U32 i = 0; i < 6; i++)
    {
        S64 index = find_first_char_from_left(str, white_space);
        String name = sub_string(str, 0, index);
        str = advance(str, name.count);
        str = eat_chars(str, white_space);
        index = find_first_char_from_left(str, white_space);
        if (index == -1)
        {
            return false;
        }
        String value = sub_string(str, 0, index);
        texture_assets[i] = str_to_u64(value);
        str = advance(str, value.count);
        str = eat_chars(str, white_space);
    }

    for (U32 i = 0; i < 6; i++)
    {
        Asset *asset = get_asset(texture_assets[i]);
        if (!asset)
        {
            return false;
        }
        skybox_resource_info.texture_resources[i] = asset->resource_refs[0];
        append(&resource->resource_refs, skybox_resource_info.texture_resources[i]);
    }

    Resource_Header header = make_resource_header(Asset_Type::SKYBOX, resource->asset_uuid, resource->uuid);
    U8 *buffer = (U8 *)(&arena->base[arena->offset]);
    U64 offset = 0;
    copy_memory(buffer, &header, sizeof(header));
    offset += sizeof(header);

    if (resource->resource_refs.count)
    {
        copy_memory(&buffer[offset], resource->resource_refs.data, sizeof(U64) * resource->resource_refs.count);
        offset += sizeof(U64) * resource->resource_refs.count;
    }

    copy_memory(buffer, &skybox_resource_info, sizeof(skybox_resource_info));
    offset += sizeof(skybox_resource_info);

    bool result = write_entire_file(resource->absolute_path, buffer, offset);
    return result;
}

bool load_skybox_resource(Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena)
{
    U64 offset = sizeof(Resource_Header) + resource->resource_refs.count * sizeof(U64);

    Skybox_Resource_Info info;
    platform_read_data_from_file(open_file_result, offset, &info, sizeof(info));
    offset += sizeof(info);

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    for (U32 i = 0; i < 6; i++)
    {
        Resource *resource = get_resource({ .uuid = info.texture_resources[i] });
        if (resource->state != Resource_State::LOADED)
        {
            return false;
        }
    }

    void *data_array[6] = {};

    U32 width = 0;
    U32 height = 0;
    Texture_Format format = Texture_Format::R8G8B8A8_UNORM;

    for (U32 i = 0; i < 6; i++)
    {
        Resource *texture_resoruce = get_resource({ .uuid = info.texture_resources[i] });
        Open_File_Result open_file_result = platform_open_file(texture_resoruce->absolute_path.data, OpenFileFlag_Read);
        if (!open_file_result.success)
        {
            return false;
        }

        HE_DEFER
        {
            platform_close_file(&open_file_result);
        };

        Texture_Resource_Info info;
        platform_read_data_from_file(&open_file_result, sizeof(Resource_Header), &info, sizeof(Texture_Resource_Info));

        width = info.width;
        height = info.height;
        format = info.format;

        U64 data_size = sizeof(U32) * info.width * info.height;
        U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, info.width * info.height);
        bool success = platform_read_data_from_file(&open_file_result, info.data_offset, data, data_size);

        data_array[i] = data;
    }

    Texture_Descriptor cubmap_texture_descriptor =
    {
        .width = width,
        .height = height,
        .format = format,
        .layer_count = (U32)6,
        .data_array = to_array_view(data_array),
        .mipmapping = true,
        .is_cubemap = true
    };

    Texture_Handle texture_handle = renderer_create_texture(cubmap_texture_descriptor);
    resource->index = texture_handle.index;
    resource->generation = texture_handle.generation;
    return true;
}

void unload_skybox_resource(Resource *resource)
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