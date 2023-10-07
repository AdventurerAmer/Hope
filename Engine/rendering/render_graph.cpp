#include "render_graph.h"
#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

void init(Render_Graph *render_graph, Allocator allocator)
{
    reset(&render_graph->nodes);
    init(&render_graph->node_cache, allocator, HE_MAX_RENDER_GRAPH_NODE_COUNT);

    reset(&render_graph->resources);
    init(&render_graph->resource_cache, allocator, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT);

    render_graph->allocator = allocator;
}

Render_Graph_Node& add_node(Render_Graph *render_graph, const char *name, const Array_View< Render_Target_Info > &render_targets, render_proc render)
{
    HE_ASSERT(find(&render_graph->node_cache, HE_STRING(name)) == -1);
    
    Render_Graph_Node &node = append(&render_graph->nodes);
    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(&node - render_graph->nodes.data);
    
    insert(&render_graph->node_cache, HE_STRING(name), node_handle);

    if (!node.edges.data)
    {
        init(&node.edges, render_graph->allocator);
    }

    reset(&node.render_targets);
    reset(&node.clear_values);
    
    for (const auto &render_target : render_targets)
    {
        String render_target_name = HE_STRING(render_target.name);
        Render_Graph_Resource_Handle resource_handle = -1;

        // todo(amer): iterator...
        S32 index = find(&render_graph->resource_cache, render_target_name);
        if (index == -1)
        {
            Render_Graph_Resource &resource = append(&render_graph->resources);
            resource.name = render_target_name;
            resource.node_handle = node_handle;
            resource.info = render_target.info;
            resource.ref_count = 0;
            
            if (render_target.info.resizable)
            {
                glm::vec2 viewport = renderer_get_viewport();
                U32 width = (U32)(render_target.info.scale_x * viewport.x);
                U32 height = (U32)(render_target.info.scale_y * viewport.y);
                resource.info.width = width;
                resource.info.height = height;
            }

            resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
            insert(&render_graph->resource_cache, resource.name, resource_handle);
        }
        else
        {
            resource_handle = render_graph->resource_cache.values[index];
        }

        append(&node.render_targets, resource_handle);
    }

    node.name = HE_STRING(name);
    node.render = render;
    node.clear_values.count = render_targets.count;
    return node;
}

Render_Graph_Node_Handle get_node(Render_Graph *render_graph, const char *name)
{
    Render_Graph_Node_Handle node_handle = -1;

    S32 index = find(&render_graph->node_cache, HE_STRING(name));
    if (index != -1)
    {
        node_handle = render_graph->node_cache.values[index];
    }
    
    return node_handle;
}

void compile(Render_Graph *render_graph, Renderer *renderer)
{
    for (Render_Graph_Node &node : render_graph->nodes)
    {
        reset(&node.edges);
    }

    for (Render_Graph_Node &node : render_graph->nodes)
    {
        Render_Graph_Node_Handle node_handle = Render_Graph_Node_Handle(&node - render_graph->nodes.data);

        for (Render_Graph_Resource_Handle &resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            if (resource.node_handle == node_handle)
            {
                continue;
            }

            Render_Graph_Node &parent = render_graph->nodes[resource.node_handle];
            if (find(&parent.edges, node_handle) == -1)
            {
                append(&parent.edges, node_handle);
            }
        } 
    }

    reset(&render_graph->visited);
    zero_memory(render_graph->visited.data, render_graph->nodes.count);

    reset(&render_graph->node_stack);
    reset(&render_graph->topologically_sorted_nodes);

    for (const Render_Graph_Node &node : render_graph->nodes)
    {
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
        
        for (Render_Graph_Resource_Handle &resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            resource.ref_count++;
        }
    }

    auto &texture_free_list = render_graph->texture_free_list;
    reset(&texture_free_list);

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > node_free_textures = {};

        for (Render_Graph_Resource_Handle resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            if (resource.node_handle == node_handle)
            {
                Texture_Descriptor texture_descriptor = {};
                texture_descriptor.width = resource.info.width;
                texture_descriptor.height = resource.info.height;
                texture_descriptor.format = resource.info.format;
                texture_descriptor.sample_count = resource.info.sample_count;
                texture_descriptor.is_attachment = true;

                Memory_Requirements texture_memory_requirments = renderer->get_texture_memory_requirements(texture_descriptor);

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    U64 best_size_so_far = HE_MAX_U64;
                    S32 best_texture_index_so_far = -1;
                    resource.info.handles[frame_index] = Resource_Pool< Texture >::invalid_handle;

                    for (U32 texture_index = 0; texture_index < texture_free_list.count; texture_index++)
                    {
                        Texture *texture = renderer_get_texture(texture_free_list[texture_index]);
                        
                        if (texture->width == resource.info.width && texture->height == resource.info.height &&
                            texture->sample_count == resource.info.sample_count && texture->format == resource.info.format)
                        {
                            resource.info.handles[frame_index] = texture_free_list[texture_index];
                            remove_and_swap_back(&texture_free_list, texture_index);
                            break;
                        }
                        else if (texture->size >= texture_memory_requirments.size && texture->alignment >= texture_memory_requirments.alignment)
                        {
                            if (texture->size < best_size_so_far)
                            {
                                best_size_so_far = texture->size;
                                best_texture_index_so_far = texture_index;
                            }
                        }
                    }

                    if (resource.info.handles[frame_index].index == -1)
                    {
                        if (best_texture_index_so_far != -1)
                        {
                            texture_descriptor.alias = texture_free_list[best_texture_index_so_far];
                            remove_and_swap_back(&texture_free_list, best_texture_index_so_far);
                        }
                        resource.info.handles[frame_index] = renderer_create_texture(texture_descriptor);
                    }
                }    
            }

            resource.ref_count--;
            if (resource.ref_count == 0)
            {
                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&node_free_textures, resource.info.handles[frame_index]);
                }
            }
        }

        for (Texture_Handle texture_handle : node_free_textures)
        {
            append(&texture_free_list, texture_handle);
        }
    }

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];
        
        Render_Pass_Descriptor render_pass_descriptor = {};
        Frame_Buffer_Descriptor frame_buffer_descriptors[HE_MAX_FRAMES_IN_FLIGHT] = {};

        U32 width = 0;
        U32 height = 0;

        for (Render_Graph_Resource_Handle resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.info.format;
            attachment_info.sample_count = resource.info.sample_count;
            attachment_info.operation = resource.info.operation;
            width = resource.info.width;
            height = resource.info.height; 

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
                append(&frame_buffer_descriptors[frame_index].attachments, resource.info.handles[frame_index]);
            }
        }

        node.render_pass = renderer_create_render_pass(render_pass_descriptor);

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            frame_buffer_descriptors[frame_index].width = width;
            frame_buffer_descriptors[frame_index].height = height;
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

        renderer->begin_render_pass(node.render_pass, node.frame_buffers[renderer_state->current_frame_in_flight_index], to_array_view(node.clear_values));
        node.render(renderer, renderer_state);
        renderer->end_render_pass(node.render_pass);
    }
}

void invalidate(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state, U32 width, U32 height)
{
    renderer->wait_for_gpu_to_finish_all_work();

    for (Render_Graph_Resource &resource : render_graph->resources)
    {
        if (resource.info.resizable)
        {
            resource.info.width = (U32)(resource.info.scale_x * width);
            resource.info.height = (U32)(resource.info.scale_y * height);
        }

        if (resource.info.resizable_sample)
        {
            resource.info.sample_count = renderer_state->sample_count;
        }

        Texture_Descriptor texture_descriptor = {};
        texture_descriptor.format = resource.info.format;
        texture_descriptor.is_attachment = true;
        texture_descriptor.width = resource.info.width;
        texture_descriptor.height = resource.info.width;
        texture_descriptor.sample_count = resource.info.sample_count;

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            if (is_valid_handle(&renderer_state->textures, resource.info.handles[frame_index]))
            {
                renderer->destroy_texture(resource.info.handles[frame_index]);
                renderer->create_texture(resource.info.handles[frame_index], texture_descriptor);
            }
        }
    }

    for (Render_Graph_Node_Handle node_handle : render_graph->topologically_sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];
        Render_Graph_Resource_Handle render_target_handle = node.render_targets[0];
        Render_Graph_Resource &render_target = render_graph->resources[render_target_handle];
        if (render_target.info.resizable || render_target.info.resizable_sample)
        {
            Render_Pass_Descriptor render_pass_descriptor = {};
            Frame_Buffer_Descriptor frame_buffer_descriptors[HE_MAX_FRAMES_IN_FLIGHT] = {};

            for (Render_Graph_Resource_Handle resource_handle : node.render_targets)
            {
                Render_Graph_Resource& resource = render_graph->resources[resource_handle];

                Attachment_Info attachment_info = {};
                attachment_info.format = resource.info.format;
                attachment_info.sample_count = resource.info.sample_count;
                attachment_info.operation = resource.info.operation;

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
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.info.handles[frame_index]);
                }

                if (is_valid_handle(&renderer_state->render_passes, node.render_pass))
                {
                    renderer->destroy_render_pass(node.render_pass);
                    renderer->create_render_pass(node.render_pass, render_pass_descriptor);
                }

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    frame_buffer_descriptors[frame_index].width = width;
                    frame_buffer_descriptors[frame_index].height = height;
                    frame_buffer_descriptors[frame_index].render_pass = node.render_pass;

                    if (is_valid_handle(&renderer_state->frame_buffers, node.frame_buffers[frame_index]))
                    {
                        renderer->destroy_frame_buffer(node.frame_buffers[frame_index]);
                        renderer->create_frame_buffer(node.frame_buffers[frame_index], frame_buffer_descriptors[frame_index]);
                    }
                }
            }

            if (render_target.info.resizable_sample)
            {
                for (auto it = iterator(&renderer_state->pipeline_states); next(&renderer_state->pipeline_states, it); )
                {
                    Pipeline_State *pipeline_state = &renderer_state->pipeline_states.data[it.index];

                    if (pipeline_state->descriptor.render_pass == node.render_pass)
                    {
                        renderer->destroy_pipeline_state(it);
                        renderer->create_pipeline_state(it, pipeline_state->descriptor);
                    }
                }
            }
        }
    }
}