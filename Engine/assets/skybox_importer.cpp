#include "assets/skybox_importer.h"

#include "core/memory.h"
#include "core/file_system.h"
#include "core/logging.h"

#include "rendering/renderer.h"

#pragma warning(push, 0)
#include <stb/stb_image.h>
#pragma warning(pop)

Load_Asset_Result load_skybox(String path)
{
    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Read_Entire_File_Result file_result = read_entire_file(path, to_allocator(scratch_memory.arena));

    String str = { .count = file_result.size, .data = (const char *)file_result.data };
    String white_space = HE_STRING_LITERAL(" \n\t\r\v\f");
    str = eat_chars(str, white_space);

    String version_lit = HE_STRING_LITERAL("version");
    if (!starts_with(str, version_lit))
    {
        HE_LOG(Assets, Error, "load_skybox -- failed to parse skybox asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    str = advance(str, version_lit.count);
    str = eat_chars(str, white_space);
    S64 index = find_first_char_from_left(str, white_space);
    if (index == -1)
    {
        HE_LOG(Assets, Error, "load_skybox -- failed to parse skybox asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    String version_value = sub_string(str, 0, index);
    U32 version = u64_to_u32(str_to_u64(version_value));
    str = advance(str, version_value.count);
    str = eat_chars(str, white_space);

    Asset_Handle texture_assets[(U32)Skybox_Face::COUNT] = {};

    for (U32 i = 0; i < (U32)Skybox_Face::COUNT; i++)
    {
        S64 index = find_first_char_from_left(str, white_space);
        String name = sub_string(str, 0, index);
        str = advance(str, name.count);
        str = eat_chars(str, white_space);
        index = find_first_char_from_left(str, white_space);
        if (index == -1)
        {
            HE_LOG(Assets, Error, "load_skybox -- failed to parse skybox asset: %.*s\n", HE_EXPAND_STRING(path));
            return {};
        }
        String value = sub_string(str, 0, index);
        texture_assets[i].uuid = str_to_u64(value);
        str = advance(str, value.count);
        str = eat_chars(str, white_space);
    }

    void *data_array[6] = {};

    U32 texture_width = 0;
    U32 texture_height = 0;
    Texture_Format format = Texture_Format::R8G8B8A8_UNORM;

    for (U32 i = 0; i < (U32)Skybox_Face::COUNT; i++)
    {
        S32 width;
        S32 height;
        S32 channels;
        const Asset_Registry_Entry &entry = get_asset_registry_entry(texture_assets[i]);

        String texture_abolute_path = format_string(scratch_memory.arena, "%.*s/%.*s", HE_EXPAND_STRING(get_asset_path()), HE_EXPAND_STRING(entry.path));
        Read_Entire_File_Result texture_file_result = read_entire_file(texture_abolute_path, to_allocator(scratch_memory.arena));
        stbi_uc *pixels = stbi_load_from_memory(texture_file_result.data, u64_to_u32(texture_file_result.size), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            HE_LOG(Assets, Error, "load_skybox -- stbi_load_from_memory -- failed to load texture asset: %.*s\n", HE_EXPAND_STRING(entry.path));
            return {};
        }

        texture_width = (U32)width;
        texture_height = (U32)height;

        U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, width * height);
        copy_memory(data, pixels, width * height * sizeof(U32));

        stbi_image_free(pixels);

        data_array[i] = data;
    }

    Texture_Descriptor cubmap_texture_descriptor =
    {
        .width = texture_width,
        .height = texture_height,
        .format = format,
        .layer_count = (U32)Skybox_Face::COUNT,
        .data_array = to_array_view(data_array),
        .mipmapping = true,
        .is_cubemap = true
    };

    Texture_Handle skybox_handle = renderer_create_texture(cubmap_texture_descriptor);
    return { .success = true, .index = skybox_handle.index, .generation = skybox_handle.generation };
}

void unload_skybox(Load_Asset_Result load_result)
{
    Texture_Handle skybox_handle = { .index = load_result.index, .generation = load_result.generation };
    renderer_destroy_texture(skybox_handle);
}