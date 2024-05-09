#include "render_graph.h"
#include "rendering/renderer.h"
#include "rendering/renderer_utils.h"

#include "core/logging.h"

void init(Render_Graph *render_graph)
{
    Allocator allocator = to_allocator(get_permenent_arena());

    reset(&render_graph->nodes);
    init(&render_graph->node_cache, HE_MAX_RENDER_GRAPH_NODE_COUNT, allocator);

    reset(&render_graph->resources);
    init(&render_graph->resource_cache, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT, allocator);

    reset(&render_graph->texture_free_list);
    
    render_graph->presentable_resource = nullptr; 
}

Render_Graph_Node& add_node(Render_Graph *render_graph, const char *name, const Array_View< Render_Target_Info > &render_targets, render_proc render, render_proc before, render_proc after)
{
    HE_ASSERT(!is_valid(find(&render_graph->node_cache, HE_STRING(name))));
    
    Render_Graph_Node &node = append(&render_graph->nodes);
    node.enabled = true;
    node.render_pass = Resource_Pool< Render_Pass >::invalid_handle;
    
    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        node.frame_buffers[frame_index] = Resource_Pool< Frame_Buffer >::invalid_handle;
    }

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(&node - render_graph->nodes.data);
    
    insert(&render_graph->node_cache, HE_STRING(name), node_handle);

    if (!node.edges.data)
    {
        init(&node.edges);
    }

    reset(&node.original_render_targets);
    reset(&node.render_targets);
    reset(&node.render_target_operations);
    reset(&node.resolve_render_targets);
    reset(&node.clear_values);
    
    for (const auto &render_target : render_targets)
    {
        String render_target_name = HE_STRING(render_target.name);
        Render_Graph_Resource_Handle resource_handle = -1;

        Hash_Map_Iterator it = find(&render_graph->resource_cache, render_target_name);
        if (!is_valid(it))
        {
            Render_Graph_Resource &resource = append(&render_graph->resources);
            resource.name = render_target_name;
            resource.node_handle = node_handle;
            resource.info = render_target.info;
            resource.resolver_handle = -1;
            resource.ref_count = 0;
            
            Render_Context context = get_render_context();
            
            if (render_target.info.resizable)
            {
                U32 width = (U32)(render_target.info.scale_x * context.renderer_state->back_buffer_width);
                U32 height = (U32)(render_target.info.scale_y * context.renderer_state->back_buffer_height);
                resource.info.width = width;
                resource.info.height = height;
            }

            if (render_target.info.resizable_sample)
            {
                resource.info.sample_count = get_sample_count(context.renderer_state->msaa_setting);
            }

            resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
            insert(&render_graph->resource_cache, resource.name, resource_handle);
        }
        else
        {
            resource_handle = *it.value;
        }
        
        append(&node.original_render_targets, resource_handle);
        append(&node.render_targets, resource_handle);
        append(&node.render_target_operations, render_target.operation);
    }

    node.name = HE_STRING(name);
    node.before = before;
    node.render = render;
    node.after = after;
    node.clear_values.count = render_targets.count;
    return node;
}

void add_resolve_color_attachment(Render_Graph *render_graph, Render_Graph_Node *node, const char *render_target, const char *resolve_render_target)
{
    String render_target_name = HE_STRING(render_target);
    String resolve_render_target_name = HE_STRING(resolve_render_target);

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);
    
    auto render_target_it = find(&render_graph->resource_cache, render_target_name);
    HE_ASSERT(is_valid(render_target_it));
    
    Render_Graph_Node_Handle render_target_resource_handle = *render_target_it.value;
    Render_Graph_Resource &render_target_resource = render_graph->resources[render_target_resource_handle];
    
    HE_ASSERT(render_target_resource.info.resizable_sample || (!render_target_resource.info.resizable_sample && render_target_resource.info.sample_count > 1));

    bool found = false;
    for (Render_Graph_Resource_Handle resource_handle : node->render_targets)
    {
        if (resource_handle == render_target_resource_handle)
        {
            found = true;
            break;
        }
    }

    HE_ASSERT(found);

    Render_Graph_Resource_Handle resource_handle = -1;

    auto resolve_render_target_it = find(&render_graph->resource_cache, resolve_render_target_name);
    if (!is_valid(resolve_render_target_it))
    {
        Render_Graph_Resource &resource = append(&render_graph->resources);
        resource.name = resolve_render_target_name;
        resource.node_handle = node_handle;
        resource.info = render_target_resource.info;

        for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            resource.info.handles[frame_index] = Resource_Pool< Texture >::invalid_handle;
        }

        resource.info.sample_count = 1;
        resource.info.resizable_sample = false;
        resource.resolver_handle = render_target_resource_handle;
        resource.ref_count = 0;
        
        if (resource.info.resizable)
        {
            Render_Context context = get_render_context();
            resource.info.width = (U32)(resource.info.scale_x * context.renderer_state->back_buffer_width);
            resource.info.height = (U32)(resource.info.scale_y * context.renderer_state->back_buffer_height);
        }

        resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
        insert(&render_graph->resource_cache, resource.name, resource_handle);
    }
    else
    {
        resource_handle = *resolve_render_target_it.value;
        Render_Graph_Resource &resource = render_graph->resources[resource_handle];
        HE_ASSERT(resource.info.sample_count == 1);
        HE_ASSERT(resource.info.resizable_sample == false);
    }

    append(&node->resolve_render_targets, resource_handle);
    node->clear_values.count++;
}

void set_presentable_attachment(Render_Graph *render_graph, const char *render_target)
{
    auto it = find(&render_graph->resource_cache, HE_STRING(render_target));
    HE_ASSERT(is_valid(it));

    Render_Graph_Resource_Handle resource_handle = *it.value;
    render_graph->presentable_resource = &render_graph->resources[resource_handle];
}

Render_Graph_Node_Handle get_node(Render_Graph *render_graph, String name)
{
    Render_Graph_Node_Handle node_handle = -1;

    auto it = find(&render_graph->node_cache, name);
    if (is_valid(it))
    {
        node_handle = *it.value;
    }
    
    return node_handle;
}

Render_Pass_Handle get_render_pass(Render_Graph *render_graph, String name)
{
    Render_Graph_Node_Handle node_handle = get_node(render_graph, name);
    
    if (node_handle == -1)
    {
        HE_LOG(Rendering, Trace, "failed to find render graph node: %s\n", name);
        return Resource_Pool< Render_Pass >::invalid_handle;
    }

    Render_Graph_Node &node = render_graph->nodes[node_handle];
    return node.render_pass;
}

bool compile(Render_Graph *render_graph, Renderer *renderer, Renderer_State *renderer_state)
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

        Render_Graph_Node_Handle node_handle = Render_Graph_Node_Handle(&node - render_graph->nodes.data);

        for (U32 render_target_index = 0; render_target_index < node.render_targets.count; render_target_index++)
        {
            Render_Graph_Resource_Handle resource_handle = node.original_render_targets[render_target_index];
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            node.render_targets[render_target_index] = resource_handle;
            
            if (resource.resolver_handle != -1)
            {
                Render_Graph_Resource &resolver = render_graph->resources[resource.resolver_handle];
                if (resolver.info.resizable_sample && renderer_state->msaa_setting == MSAA_Setting::NONE)
                {
                    node.render_targets[render_target_index] = resource.resolver_handle;
                }
            }

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

        for (Render_Graph_Resource_Handle &resource_handle : node.resolve_render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            if (resource.node_handle == node_handle)
            {
                continue;
            }
            
            Render_Graph_Resource &resolver = render_graph->resources[resource.resolver_handle];
            if (resolver.info.resizable_sample && renderer_state->msaa_setting == MSAA_Setting::NONE)
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
                else if (render_graph->visited[child_handle] == 1)
                {
                    return false;
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

    return true;
}

void render(Render_Graph *render_graph, Renderer *renderer, Renderer_State *renderer_state)
{
    auto &sorted_nodes = render_graph->topologically_sorted_nodes;

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        Frame_Buffer *frame_buffer = renderer_get_frame_buffer(node.frame_buffers[renderer_state->current_frame_in_flight_index]);
        renderer->set_viewport(frame_buffer->width, frame_buffer->height);

        if (node.before)
        {
            node.render(renderer, renderer_state);
        }

        renderer->begin_render_pass(node.render_pass, node.frame_buffers[renderer_state->current_frame_in_flight_index], to_array_view(node.clear_values));
        node.render(renderer, renderer_state);
        renderer->end_render_pass(node.render_pass);

        if (node.after)
        {
            node.after(renderer, renderer_state);
        }
    }
}

void invalidate(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state)
{
    auto &sorted_nodes = render_graph->topologically_sorted_nodes;
    
    for (const Render_Graph_Node_Handle &node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];
        
        for (Render_Graph_Resource_Handle &resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            resource.ref_count++;
        }

        for (Render_Graph_Resource_Handle &resource_handle : node.resolve_render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            Render_Graph_Resource &resolver = render_graph->resources[resource.resolver_handle];
            if (resolver.info.resizable_sample && renderer_state->msaa_setting == MSAA_Setting::NONE)
            {
                continue;
            }
            resource.ref_count++;
        }
    }

    auto &texture_free_list = render_graph->texture_free_list;

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        Counted_Array< Texture_Handle, HE_MAX_ATTACHMENT_COUNT > node_free_textures = {};

        for (Render_Graph_Resource_Handle resource_handle : node.render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            if (resource.node_handle == node_handle)
            {
                if (resource.info.resizable)
                {
                    resource.info.width = (U32)(renderer_state->back_buffer_width * resource.info.scale_x);
                    resource.info.height = (U32)(renderer_state->back_buffer_height * resource.info.scale_y);
                } 
                
                if (resource.info.resizable_sample)
                {
                    resource.info.sample_count = get_sample_count(renderer_state->msaa_setting);    
                }

                Texture_Descriptor texture_descriptor =
                {
                    .width = resource.info.width,
                    .height = resource.info.height,
                    .format = resource.info.format,
                    .sample_count = resource.info.sample_count,
                    .is_attachment = true
                };
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
                            Texture_Handle best_texture_handle = texture_free_list[best_texture_index_so_far];
                            Texture *best_texture = renderer_get_texture(best_texture_handle);

                            if (best_texture->alias == Resource_Pool< Texture >::invalid_handle)
                            {
                                texture_descriptor.alias = best_texture_handle;
                            }
                            else
                            {
                                texture_descriptor.alias = best_texture->alias;
                            }

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

        for (Render_Graph_Resource_Handle resource_handle : node.resolve_render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            Render_Graph_Resource &resolver = render_graph->resources[resource.resolver_handle];

            if (resolver.info.resizable_sample && renderer_state->msaa_setting == MSAA_Setting::NONE)
            {
                continue;
            }

            if (resource.node_handle == node_handle)
            {
                if (resource.info.resizable)
                {
                    resource.info.width = (U32)(renderer_state->back_buffer_width * resource.info.scale_x);
                    resource.info.height = (U32)(renderer_state->back_buffer_height * resource.info.scale_y);
                }

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
                            Texture_Handle best_texture_handle = texture_free_list[best_texture_index_so_far];
                            Texture *best_texture = renderer_get_texture(best_texture_handle);

                            if (best_texture->alias == Resource_Pool< Texture >::invalid_handle)
                            {
                                texture_descriptor.alias = best_texture_handle;
                            }
                            else
                            {
                                texture_descriptor.alias = best_texture->alias;
                            }

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
        render_pass_descriptor.name = node.name;
        
        Frame_Buffer_Descriptor frame_buffer_descriptors[HE_MAX_FRAMES_IN_FLIGHT] = {};

        U32 width = 0;
        U32 height = 0;

        for (U32 render_target_index = 0; render_target_index < node.render_targets.count; render_target_index++)
        {
            Render_Graph_Resource_Handle resource_handle = node.render_targets[render_target_index];
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.info.format;
            attachment_info.sample_count = resource.info.sample_count;
            attachment_info.operation = node.render_target_operations[render_target_index];

            width = resource.info.width;
            height = resource.info.height; 

            if (is_color_format(resource.info.format))
            {
                append(&render_pass_descriptor.color_attachments, attachment_info);   
            }
            else
            {
                append(&render_pass_descriptor.depth_stencil_attachments, attachment_info);
            }

            for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
            {
                append(&frame_buffer_descriptors[frame_index].attachments, resource.info.handles[frame_index]);
            }
        }

        for (Render_Graph_Resource_Handle resource_handle : node.resolve_render_targets)
        {
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            Render_Graph_Resource &resolver = render_graph->resources[resource.resolver_handle];

            if (resolver.info.resizable_sample && renderer_state->msaa_setting == MSAA_Setting::NONE)
            {
                continue;
            }

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.info.format;
            attachment_info.sample_count = resource.info.sample_count;
            attachment_info.operation = Attachment_Operation::DONT_CARE;

            append(&render_pass_descriptor.resolve_attachments, attachment_info);

            for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
            {
                append(&frame_buffer_descriptors[frame_index].attachments, resource.info.handles[frame_index]);
            }
        }

        if (is_valid_handle(&renderer_state->render_passes, node.render_pass))
        {
            renderer->destroy_render_pass(node.render_pass);
            renderer->create_render_pass(node.render_pass, render_pass_descriptor);
        }
        else
        {
            node.render_pass = renderer_create_render_pass(render_pass_descriptor);
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
            else
            {
                node.frame_buffers[frame_index] = renderer_create_frame_buffer(frame_buffer_descriptors[frame_index]);
            }
        }
        
        if (render_graph->resources[ node.render_targets[0] ].info.resizable_sample)
        {
            for (auto it = iterator(&renderer_state->pipeline_states); next(&renderer_state->pipeline_states, it); )
            {
                Pipeline_State *pipeline_state = &renderer_state->pipeline_states.data[it.index];

                if (pipeline_state->render_pass == node.render_pass)
                {
                    renderer->destroy_pipeline_state(it);
                    
                    Pipeline_State_Descriptor pipeline_state_desc =
                    {
                        .settings = pipeline_state->settings,
                        .shader = pipeline_state->shader,
                        .render_pass = pipeline_state->render_pass,
                    };

                    renderer->create_pipeline_state(it, pipeline_state_desc);
                }
            }
        }
    }
}

Texture_Handle get_presentable_attachment(Render_Graph *render_graph, Renderer_State *renderer_state)
{
    Texture_Handle presentable_attachment = Resource_Pool< Texture >::invalid_handle;

    if (renderer_state->msaa_setting == MSAA_Setting::NONE && render_graph->presentable_resource->resolver_handle != -1)
    {
        Render_Graph_Resource_Handle resolver_handle = render_graph->presentable_resource->resolver_handle;
        Render_Graph_Resource &resolver = render_graph->resources[resolver_handle];
        presentable_attachment = resolver.info.handles[renderer_state->current_frame_in_flight_index];
    }
    else
    {
        presentable_attachment = render_graph->presentable_resource->info.handles[renderer_state->current_frame_in_flight_index];
    }

    return presentable_attachment;
}

Texture_Handle get_texture_resource(Render_Graph *render_graph, Renderer_State *renderer_state, String name)
{
    auto it = find(&render_graph->resource_cache, name);
    if (!is_valid(it))
    {
        return Resource_Pool< Texture >::invalid_handle;
    }

    const Render_Graph_Resource &resource = render_graph->resources[*it.value];
    Texture_Handle result = Resource_Pool< Texture >::invalid_handle;

    if (renderer_state->msaa_setting == MSAA_Setting::NONE && resource.resolver_handle != -1)
    {
        Render_Graph_Resource_Handle resolver_handle = render_graph->presentable_resource->resolver_handle;
        Render_Graph_Resource &resolver = render_graph->resources[resolver_handle];
        result = resolver.info.handles[renderer_state->current_frame_in_flight_index];
    }
    else
    {
        result = resource.info.handles[renderer_state->current_frame_in_flight_index];
    }

    return result;
}