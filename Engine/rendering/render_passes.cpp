#include "render_passes.h"
#include "renderer.h"

#include "core/file_system.h"

void geometry_pass(Renderer *renderer, Renderer_State *renderer_state);
void opaque_pass(Renderer *renderer, Renderer_State *renderer_state);
void ui_pass(Renderer *renderer, Renderer_State *renderer_state);

void setup_render_passes(Render_Graph *render_graph, Renderer_State *renderer_state)
{
    // geometry pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "ms_scene",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::R32_SINT,
                    .resizable_sample = true,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            },
            {
                .name = "depth",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::DEPTH_F32_STENCIL_U8,
                    .resizable_sample = true,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            }
        };

        Render_Graph_Node &node = add_node(render_graph, "geometry", to_array_view(render_targets), &geometry_pass);
        add_resolve_color_attachment(render_graph, &node, "ms_scene", "scene");
        node.clear_values[0].icolor = { -1, -1, -1, -1 };
        node.clear_values[1] = { .depth = 1.0f, .stencil = 0 };
    }

    // opaque pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "ms_main",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::R8G8B8A8_UNORM,
                    .resizable_sample = true,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            },
            {
                .name = "depth",
                .operation = Attachment_Operation::LOAD,
            }
        };

        Render_Graph_Node &node = add_node(render_graph, "opaque", to_array_view(render_targets), &opaque_pass);
        add_resolve_color_attachment(render_graph, &node, "ms_main", "main");
        node.clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    }

    // ui pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "main",
                .operation = Attachment_Operation::LOAD
            }
        };

        add_node(render_graph, "ui", to_array_view(render_targets), &ui_pass);
    }

    set_presentable_attachment(render_graph, "main");
}

static void geometry_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    if (render_data->current_pipeline_state_handle != renderer_state->geometry_pipeline)
    {
        render_data->current_pipeline_state_handle = renderer_state->geometry_pipeline;
        renderer->set_pipeline_state(renderer_state->geometry_pipeline);
    }
    
    for (U32 draw_command_index = 0; draw_command_index < render_data->opaque_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->opaque_commands[draw_command_index];
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }
}

static void opaque_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    Frame_Render_Data *render_data = &renderer_state->render_data;

    if (render_data->skyboxes_commands.count)
    {
        const Draw_Command &dc = back(&render_data->skyboxes_commands);
        renderer_use_material(dc.material);
        renderer_use_static_mesh(dc.static_mesh);
        renderer->draw_sub_mesh(dc.static_mesh, dc.instance_index, dc.sub_mesh_index);
    }

    for (U32 draw_command_index = 0; draw_command_index < render_data->opaque_commands.count; draw_command_index++)
    {
        const Draw_Command *dc = &render_data->opaque_commands[draw_command_index];
        renderer_use_material(dc->material);
        renderer_use_static_mesh(dc->static_mesh);
        renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    }
}

static void ui_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    renderer->imgui_render();
}