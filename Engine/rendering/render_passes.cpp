#include "render_passes.h"
#include "renderer.h"

#include "core/file_system.h"

#include "rendering/vulkan/vulkan_renderer.h"

void depth_prepass(Renderer *renderer, Renderer_State *renderer_state);
void opaque_pass(Renderer *renderer, Renderer_State *renderer_state);
void transparent_pass(Renderer *renderer, Renderer_State *renderer_state);
void after_transparent_pass(Renderer *renderer, Renderer_State *renderer_state);
void color_pass(Renderer *renderer, Renderer_State *renderer_state);
void outline_pass(Renderer *renderer, Renderer_State *renderer_state);
void ui_pass(Renderer *renderer, Renderer_State *renderer_state);

void setup_render_passes(Render_Graph *render_graph, Renderer_State *renderer_state)
{
    // depth prepass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "scene",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::R32_SINT,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                },
            },
            {
                .name = "depth",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::DEPTH_F32_STENCIL_U8,
                    .resizable = true,
                    .scale_x = 1.0f,
                    .scale_y = 1.0f,
                }
            }
        };

        Render_Graph_Node &node = add_node(render_graph, "depth_prepass", to_array_view(render_targets), &depth_prepass);
        node.clear_values[0].icolor = { -1, -1, -1, -1 };
        node.clear_values[1] = { .depth = 1.0f, .stencil = 0 };
    }

    // opaque pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "main",
                .operation = Attachment_Operation::CLEAR,
                .info =
                {
                    .format = Texture_Format::R8G8B8A8_UNORM,
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

        Render_Graph_Node &node = add_node(render_graph, "opaque", to_array_view(render_targets), &opaque_pass, nullptr, &after_transparent_pass);
        node.clear_values[0].color = { 1.0f, 0.0f, 1.0f, 1.0f };
    }

    // transparent pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "main",
                .operation = Attachment_Operation::LOAD,
            },
        };

        Render_Graph_Node &node = add_node(render_graph, "transparent", to_array_view(render_targets), &transparent_pass);
    }

    // outline pass
    // {
    //     Render_Target_Info render_targets[] =
    //     {
    //         {
    //             .name = "main",
    //             .operation = Attachment_Operation::LOAD,
    //         },
    //         {
    //             .name = "depth",
    //             .operation = Attachment_Operation::CLEAR,
    //         }
    //     };

    //     Render_Graph_Node &node = add_node(render_graph, "outline", to_array_view(render_targets), &outline_pass);
    //     node.clear_values[1] = { .depth = 1.0f, .stencil = 0 };
    // }


    // ui pass
    {
        Render_Target_Info render_targets[] =
        {
            {
                .name = "main",
                .operation = Attachment_Operation::LOAD,
            },
        };

        Render_Graph_Node &node = add_node(render_graph, "ui", to_array_view(render_targets), &ui_pass);
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

static void opaque_pass(Renderer *renderer, Renderer_State *renderer_state)
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
    // Frame_Render_Data *render_data = &renderer_state->render_data;

    // for (U32 draw_command_index = 0; draw_command_index < render_data->transparent_commands.count; draw_command_index++)
    // {
    //     const Draw_Command *dc = &render_data->transparent_commands[draw_command_index];
    //     renderer_use_material(dc->material);
    //     renderer_use_static_mesh(dc->static_mesh);
    //     renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    // }


    // vulkan_renderer_barrier();
    renderer->set_pipeline_state(renderer_state->transparent_pipeline);

    renderer_use_static_mesh(renderer_state->default_static_mesh);
    renderer->draw_fullscreen_triangle();
}

void after_transparent_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    vulkan_renderer_barrier();
}

static void outline_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    // Frame_Render_Data *render_data = &renderer_state->render_data;

    // renderer_use_material(renderer_state->outline_first_pass);

    // for (U32 draw_command_index = 0; draw_command_index < render_data->outline_commands.count; draw_command_index++)
    // {
    //     const Draw_Command *dc = &render_data->outline_commands[draw_command_index];
    //     renderer_use_static_mesh(dc->static_mesh);
    //     renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    // }

    // renderer_use_material(renderer_state->outline_second_pass);

    // for (U32 draw_command_index = 0; draw_command_index < render_data->outline_commands.count; draw_command_index++)
    // {
    //     const Draw_Command *dc = &render_data->outline_commands[draw_command_index];
    //     renderer_use_static_mesh(dc->static_mesh);
    //     renderer->draw_sub_mesh(dc->static_mesh, dc->instance_index, dc->sub_mesh_index);
    // }
}

static void ui_pass(Renderer *renderer, Renderer_State *renderer_state)
{
    renderer->imgui_render();
}