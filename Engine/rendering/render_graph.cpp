#include "render_graph.h"
#include "rendering/renderer.h"

void init(Render_Graph *render_graph, Allocator allocator)
{
    reset(&render_graph->resources);
    reset(&render_graph->nodes);
    init(&render_graph->resource_cache, allocator, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT);
    render_graph->allocator = allocator;
}

void add_node(Render_Graph *render_graph, const String &name, const Array_View< Render_Graph_Node_Input > &inputs, const Array_View< Render_Graph_Node_Output > &outputs, pre_render_pass_proc pre_render, render_pass_proc render)
{
    Render_Graph_Node &node = append(&render_graph->nodes);
    node.pre_render = pre_render;
    node.render = render;
    node.enabled = true;
    
    if (!node.edges.data)
    {
        init(&node.edges, render_graph->allocator);
    }

    node.name = name;
    
    reset(&node.inputs);
    reset(&node.outputs);
    
    for (const auto &input : inputs)
    {
        Render_Graph_Resource &resource = append(&render_graph->resources);
        resource.name = input.name;
        resource.type = input.type;
        resource.node_handle = -1;
        resource.output_handle = -1;
        resource.info = {};
        resource.ref_count = 0;

        Render_Graph_Resource_Handle resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
        append(&node.inputs, resource_handle);
    }

    for (const auto &output : outputs)
    {
        Render_Graph_Resource &resource = append(&render_graph->resources);
        resource.name = output.name;
        resource.type = output.type;
        resource.info = {};
        resource.ref_count = 0;
        resource.node_handle = -1;

        Render_Graph_Resource_Handle resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
        
        if (output.type != Render_Graph_Resource_Type::REFERENCE) 
        {
            Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(&node - render_graph->nodes.data);
            
            resource.node_handle = node_handle;
            resource.output_handle = resource_handle;
            resource.info = output.info;
            
            insert(&render_graph->resource_cache, resource.name, resource_handle);
        }

        append(&node.outputs, resource_handle);
    }
}

void compile(Render_Graph *render_graph)
{
    for (Render_Graph_Node &node : render_graph->nodes)
    {
        reset(&node.edges);
    }

    for (Render_Graph_Node &node : render_graph->nodes)
    {
        if (!node.enabled)
        {
            continue;
        }

        for (Render_Graph_Resource_Handle &input_resource_handle : node.inputs)
        {
            Render_Graph_Resource &input_resource = render_graph->resources[input_resource_handle];

            // todo(amer): find should return an iterator...
            S32 index = find(&render_graph->resource_cache, input_resource.name); 
            HE_ASSERT(index != -1);
            Render_Graph_Resource_Handle output_resource_handle = render_graph->resource_cache.values[index];
            Render_Graph_Resource &output_resource = render_graph->resources[output_resource_handle];
            input_resource.node_handle = output_resource.node_handle;
            input_resource.info = output_resource.info;
            input_resource.output_handle = output_resource.output_handle;

            Render_Graph_Node &parent = render_graph->nodes[output_resource.node_handle];
            append(&parent.edges, Render_Graph_Node_Handle(&node - render_graph->nodes.data));
        } 
    }

    reset(&render_graph->visited);
    zero_memory(render_graph->visited.data, render_graph->nodes.count);

    reset(&render_graph->node_stack);
    reset(&render_graph->topologically_sorted_nodes);

    for (const Render_Graph_Node &node : render_graph->nodes)
    {
        if (!node.enabled)
        {
            continue;
        }

        append(&render_graph->node_stack, (Render_Graph_Node_Handle)(&node - render_graph->nodes.data));

        while (render_graph->node_stack.count)
        {
            Render_Graph_Node_Handle current_node_handle = back(&render_graph->node_stack);
            
            if (render_graph->visited[current_node_handle] == 2)
            {
                remove_back(&render_graph->node_stack);
                continue;
            }
            else if (render_graph->visited[current_node_handle] == 1)
            {
                render_graph->visited[current_node_handle] = 2;

                append(&render_graph->topologically_sorted_nodes, current_node_handle);
                remove_back(&render_graph->node_stack);
                continue;
            }

            render_graph->visited[current_node_handle] = 1;

            Render_Graph_Node &current_node = render_graph->nodes[current_node_handle];

            for (const Render_Graph_Node_Handle &child_handle : current_node.edges)
            {
                if (!render_graph->visited[child_handle])
                {
                    append(&render_graph->node_stack, child_handle);
                }
            }
        }
    }

    auto &sorted_nodes = render_graph->topologically_sorted_nodes;

    for (U32 node_index = 0; node_index < sorted_nodes.count / 2; node_index++)
    {
        Render_Graph_Node_Handle temp = sorted_nodes[node_index];
        U32 corresponding_node_index = sorted_nodes.count - node_index - 1;
        sorted_nodes[node_index] = sorted_nodes[corresponding_node_index];
        sorted_nodes[corresponding_node_index] = temp; 
    }

    for (const Render_Graph_Node_Handle &node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];
        
        for (Render_Graph_Resource_Handle &input_resource_handle : node.inputs)
        {
            Render_Graph_Resource &input_resource = render_graph->resources[input_resource_handle];
            Render_Graph_Resource &resource = render_graph->resources[input_resource.output_handle];
            resource.ref_count++;
        }
    }

    auto &texture_free_list = render_graph->texture_free_list;
    reset(&texture_free_list);

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        for (Render_Graph_Resource_Handle resource_output_handle : node.outputs)
        {
            Render_Graph_Resource &output_resource = render_graph->resources[resource_output_handle];
            if (output_resource.type == Render_Graph_Resource_Type::ATTACHMENT)
            {
                // todo(amer): texture aliasing and best fit
                Texture_Descriptor texture_descriptor = {};
                texture_descriptor.width = output_resource.info.texture.width;
                texture_descriptor.height = output_resource.info.texture.height;
                texture_descriptor.format = output_resource.info.texture.format;
                texture_descriptor.sample_count = output_resource.info.texture.sample_count;
                texture_descriptor.is_attachment = true;

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    Texture_Handle texture_handle = renderer_create_texture(texture_descriptor);
                    output_resource.info.texture.handles[frame_index] = texture_handle;
                }
            }
        }

        for (Render_Graph_Resource_Handle input_resource_handle : node.inputs)
        {
            Render_Graph_Resource &input_resource = render_graph->resources[input_resource_handle];
            Render_Graph_Resource &output_resource = render_graph->resources[input_resource.output_handle];
            output_resource.ref_count--;
            if (output_resource.ref_count == 0)
            {
                if (output_resource.type == Render_Graph_Resource_Type::ATTACHMENT || output_resource.type == Render_Graph_Resource_Type::TEXTURE)
                {
                    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                    {
                        append(&texture_free_list, output_resource.info.texture.handles[frame_index]);
                    }
                }
            }
        }
    }

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];
        
        Render_Pass_Descriptor render_pass_descriptor = {};
        Frame_Buffer_Descriptor frame_buffer_descriptors[HE_MAX_FRAMES_IN_FLIGHT] = {};

        for (Render_Graph_Resource_Handle output_resource_handle : node.outputs)
        {
            Render_Graph_Resource &resource = render_graph->resources[output_resource_handle];
            if (resource.type == Render_Graph_Resource_Type::ATTACHMENT)
            {
                Attachment_Info attachment_info = {};
                attachment_info.format = resource.info.texture.format;
                attachment_info.sample_count = resource.info.texture.sample_count;
                attachment_info.operation = resource.info.texture.operation;
                 
                if (attachment_info.format == Texture_Format::DEPTH_F32_STENCIL_U8)
                {
                    append(&render_pass_descriptor.depth_stencil_attachments, attachment_info);
                }
                else
                {
                    if (render_pass_descriptor.color_attachments.count == 0 || attachment_info.sample_count == render_pass_descriptor.color_attachments[0].sample_count)
                    {
                        append(&render_pass_descriptor.color_attachments, attachment_info);
                    }
                    else
                    {
                        append(&render_pass_descriptor.resolve_attachments, attachment_info);
                    }
                }

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.info.texture.handles[frame_index]);
                }
            }
        }

        for (Render_Graph_Resource_Handle input_resource_handle : node.inputs)
        {
            Render_Graph_Resource &input_resource = render_graph->resources[input_resource_handle];
            Render_Graph_Resource &output_resource = render_graph->resources[input_resource.output_handle];

            if (input_resource.type == Render_Graph_Resource_Type::ATTACHMENT)
            {
                Attachment_Info attachment_info = {};
                attachment_info.format = output_resource.info.texture.format;
                attachment_info.sample_count = output_resource.info.texture.sample_count;
                attachment_info.operation = output_resource.info.texture.operation;
                
                if (attachment_info.format == Texture_Format::DEPTH_F32_STENCIL_U8)
                {
                    append(&render_pass_descriptor.depth_stencil_attachments, attachment_info);
                }
                else
                {
                    if (render_pass_descriptor.color_attachments.count == 0 || attachment_info.sample_count == render_pass_descriptor.color_attachments[0].sample_count)
                    {
                        append(&render_pass_descriptor.color_attachments, attachment_info);
                    }
                    else
                    {
                        append(&render_pass_descriptor.resolve_attachments, attachment_info);
                    }
                }

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, output_resource.info.texture.handles[frame_index]);
                }
            }
        }

        node.render_pass = renderer_create_render_pass(render_pass_descriptor);

        glm::vec2 viewport = renderer_get_viewport();

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            frame_buffer_descriptors[frame_index].width = (U32)viewport.x;
            frame_buffer_descriptors[frame_index].height = (U32)viewport.y;
            frame_buffer_descriptors[frame_index].render_pass = node.render_pass;
            node.frame_buffers[frame_index] = renderer_create_frame_buffer(frame_buffer_descriptors[frame_index]);
        }
    }
}

void render(Render_Graph *render_graph, Renderer *renderer, Renderer_State *renderer_state)
{
    auto &sorted_nodes = render_graph->topologically_sorted_nodes;

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        Frame_Buffer *frame_buffer = renderer_get_frame_buffer(node.frame_buffers[renderer_state->current_frame_in_flight_index]);
        renderer->set_viewport(frame_buffer->width, frame_buffer->height);

        Array< Clear_Value, HE_MAX_ATTACHMENT_COUNT > clear_values = node.pre_render(renderer, renderer_state);
        renderer->begin_render_pass(node.render_pass, node.frame_buffers[renderer_state->current_frame_in_flight_index], to_array_view(clear_values));
        node.render(renderer, renderer_state);
        renderer->end_render_pass(node.render_pass);
    }
}