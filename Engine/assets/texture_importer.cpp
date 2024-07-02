#include "assets/texture_importer.h"
#include "core/memory.h"
#include "core/file_system.h"
#include "core/logging.h"

#include "rendering/renderer.h"

#pragma warning(push, 0)

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#pragma warning(pop)

Load_Asset_Result load_texture(String path, const Embeded_Asset_Params *params)
{
    Memory_Context memory_context = grab_memory_context();
    
    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    Read_Entire_File_Result file_result = read_entire_file(path, memory_context.temp_allocator);
    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "load_texture -- failed to read file: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }
    
    String extension = get_extension(path);
    bool is_hdr = extension == "hdr";

    S32 width = 0;
    S32 height = 0;
    S32 channels = 0;

    stbi_uc *pixels = stbi_load_from_memory(file_result.data, u64_to_u32(file_result.size), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        HE_LOG(Assets, Error, "load_texture -- stbi_load_from_memory -- failed to load texture asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    U32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, U32, width * height);
    copy_memory(data, pixels, width * height * sizeof(U32));
    stbi_image_free(pixels);

    void *data_array[] = { data };

    Texture_Descriptor texture_descriptor =
    {
        .name = get_name(path),
        .width = (U32)width,
        .height = (U32)height,
        .format = Texture_Format::R8G8B8A8_UNORM,
        .data_array = to_array_view(data_array),
        .mipmapping = true,
        .sample_count = 1,
    };

    Texture_Handle texture_handle = renderer_create_texture(texture_descriptor);
    if (!is_valid_handle(&renderer_state->textures, texture_handle))
    {
        HE_LOG(Assets, Error, "load_texture -- renderer_create_texture -- failed to load texture asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    return { .success = true, .index = texture_handle.index, .generation = texture_handle.generation };
}

void unload_texture(Load_Asset_Result load_result)
{
    Texture_Handle texture_handle = { .index = load_result.index, .generation = load_result.generation };
    renderer_destroy_texture(texture_handle);
}

Load_Asset_Result load_environment_map(String path, const Embeded_Asset_Params *params)
{
    Memory_Context memory_context = grab_memory_context();

    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    Read_Entire_File_Result file_result = read_entire_file(path, memory_context.temp_allocator);
    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "load_texture -- failed to read file: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    String extension = get_extension(path);
    bool is_hdr = extension == "hdr";
    HE_ASSERT(is_hdr);

    S32 width = 0;
    S32 height = 0;
    S32 channels = 0;

    F32 *pixels = stbi_loadf_from_memory(file_result.data, u64_to_u32(file_result.size), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        HE_LOG(Assets, Error, "load_texture -- stbi_loadf_from_memory -- failed to load texture asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    F32 *data = HE_ALLOCATE_ARRAY(&renderer_state->transfer_allocator, F32, 4 * width * height);
    copy_memory(data, pixels, width * height * 4 * sizeof(F32));
    stbi_image_free(pixels);

    Environment_Map *environment_map = HE_ALLOCATOR_ALLOCATE(memory_context.general_allocator, Environment_Map);
    *environment_map = renderer_hdr_to_environment_map(data, width, height);
    deallocate(&renderer_state->transfer_allocator, (void *)data);

    return { .success = true, .data = (void *)environment_map, .size = sizeof(Environment_Map) };
}

void unload_environment_map(Load_Asset_Result load_result)
{
    Memory_Context memory_context = grab_memory_context();

    Environment_Map *environment_map = (Environment_Map *)load_result.data;
    renderer_destroy_texture(environment_map->environment_map);
    renderer_destroy_texture(environment_map->irradiance_map);

    HE_ALLOCATOR_DEALLOCATE(memory_context.general_allocator, (void *)environment_map);
}