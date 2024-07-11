#include "render_passes.h"
#include "renderer.h"

#include "core/file_system.h"

#include "rendering/vulkan/vulkan_renderer.h"

void depth_prepass(Renderer *renderer, Renderer_State *renderer_state);

void world_pass(Renderer *renderer, Renderer_State *renderer_state);

void transparent_pass(Renderer *renderer, Renderer_State *renderer_state);

void ui_pass(Renderer *renderer, Renderer_State *renderer_state);

void setup_render_passes(Render_Graph *render_graph, Renderer_State *renderer_state)
{
    using enum Attachment_Operation;
    using enum Texture_Format;

    // depth prepass
    {
        Render_Graph_Node &depth = add_graphics_node(render_graph, "depth_prepass", &depth_prepass);
        add_render_target(render_graph, &depth, "scene", { .format = R32_SINT }, CLEAR, { .icolor = { -1, -1, -1, -1 } });
        add_depth_stencil_target(render_graph, &depth, "depth", { .format = DEPTH_F32_STENCIL_U8 }, CLEAR, { .depth = 1.0f, .stencil = 0 });
    }

    // world pass
    {
        Render_Graph_Node &world = add_graphics_node(render_graph, "world", &world_pass);
        add_render_target(render_graph, &world, "rt0", { .format = R32G32B32A32_SFLOAT }, CLEAR, { .color = { 1.0f, 0.0f, 1.0f, 1.0f } });
        set_depth_stencil_target(render_graph, &world, "depth", LOAD);
        add_storage_texture(render_graph, &world, "head_index_image", { .format = R32_UINT }, { .ucolor = { HE_MAX_U32, HE_MAX_U32, HE_MAX_U32, HE_MAX_U32 } });

        add_storage_buffer(render_graph, &world, "nodes", { .size = sizeof(Shader_Node) * 20, .resizable = true }, HE_MAX_U32);
        add_storage_buffer(render_graph, &world, "node_count", { .size = sizeof(U32) }, 0);
    }

    // transparent pass
    {
        Render_Graph_Node &transparent = add_graphics_node(render_graph, "transparent", &transparent_pass);
        add_render_target_input(render_graph, &transparent, "scene", LOAD);
        add_render_target(render_graph, &transparent, "main", { .format = R8G8B8A8_UNORM }, CLEAR, { .color = { 1.0f, 0.0f, 1.0f, 1.0f } });

        add_texture_input(render_graph, &transparent, "rt0");
        add_storage_texture_input(render_graph, &transparent, "head_index_image");
        add_storage_buffer_input(render_graph, &transparent, "nodes");
        add_storage_buffer_input(render_graph, &transparent, "node_count");
    }

    // ui pass
    {
        Render_Graph_Node &ui = add_graphics_node(render_graph, "ui", &ui_pass);
        add_render_target_input(render_graph, &ui, "main", LOAD);
        set_depth_stencil_target(render_graph, &ui, "depth", CLEAR, { .depth = 1.0f, .stencil = 0 });
    }

    set_presentable_attachment(render_graph, "main");
}

static void depth_prepass(Renderer *renderer, Renderer_State *renderer_state)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;
    renderer->set_pipeline_state(renderer_state->depth_prepass_pipeline);

    for (U32 draw_command_index = 0; draw_command_index < render_data->opaque_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->opaque_commands[draw_command_index];
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }
}

static void world_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    for (U32 draw_command_index = 0; draw_command_index < render_data->opaque_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->opaque_commands[draw_command_index];
        renderer_use_material(dc->material);
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }

    for (U32 draw_command_index = 0; draw_command_index < render_data->alpha_cutoff_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->alpha_cutoff_commands[draw_command_index];
        renderer_use_material(dc->material);
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }

    if (render_data->skybox_commands.count)
    {
        const Draw_Command &dc = back(&render_data->skybox_commands);
        renderer_use_material(dc.material);
        renderer_use_static_mesh(dc.static_mesh);
        renderer->draw_sub_mesh(dc.static_mesh, dc.instance_index, dc.sub_mesh_index);
    }

    for (U32 draw_command_index = 0; draw_command_index < render_data->transparent_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->transparent_commands[draw_command_index];
        renderer_use_material(dc->material);
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }
}

void transparent_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    renderer->set_pipeline_state(renderer_state->transparent_pipeline);

    renderer_use_static_mesh(renderer_state->default_static_mesh);
    renderer->draw_fullscreen_triangle();
}

static void ui_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    renderer_use_material(renderer_state->outline_first_pass);

    for (U32 draw_command_index = 0; draw_command_index < render_data->outline_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->outline_commands[draw_command_index];
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }

    renderer_use_material(renderer_state->outline_second_pass);

    for (U32 draw_command_index = 0; draw_command_index < render_data->outline_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->outline_commands[draw_command_index];
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }

    renderer->imgui_render();
}