#include "shader_importer.h"

#include "core/logging.h"
#include "core/file_system.h"

#include "rendering/renderer.h"

Load_Asset_Result load_shader(String path, const Embeded_Asset_Params *params)
{
    Temprary_Memory_Arena_Janitor scratch_memory = make_scratch_memory_janitor();
    Render_Context render_context = get_render_context();
    Renderer_State *renderer_state = render_context.renderer_state;

    Read_Entire_File_Result file_result = read_entire_file(path, to_allocator(scratch_memory.arena));
    if (!file_result.success)
    {
        HE_LOG(Assets, Error, "load_shader -- failed to read asset file: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    String source = { .count = file_result.size, .data = (const char *)file_result.data };
    String include_path = get_parent_path(path);
    Shader_Compilation_Result compilation_result = renderer_compile_shader(source, include_path);
    if (!compilation_result.success)
    {
        HE_LOG(Assets, Error, "load_shader -- failed to compile shader asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    HE_DEFER
    {
        renderer_destroy_shader_compilation_result(&compilation_result);
    };

    Shader_Descriptor shader_descriptor =
    {
        .name = get_name(path),
        .compilation_result = &compilation_result
    };

    Shader_Handle shader_handle = renderer_create_shader(shader_descriptor);
    if (!is_valid_handle(&renderer_state->shaders, shader_handle))
    {
        HE_LOG(Assets, Error, "load_shader -- failed to aquire shader handle when loading shader asset: %.*s\n", HE_EXPAND_STRING(path));
        return {};
    }

    return { .success = true, .index = shader_handle.index, .generation = shader_handle.generation };
}

void unload_shader(Load_Asset_Result load_result)
{
    Shader_Handle shader_handle = { .index = load_result.index, .generation = load_result.generation };
    renderer_destroy_shader(shader_handle);
}
