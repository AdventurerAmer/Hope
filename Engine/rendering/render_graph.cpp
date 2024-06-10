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

    render_graph->presentable_resource = nullptr; 
}

Render_Graph_Node& add_node(Render_Graph *render_graph, const char *node_name, render_proc before_render, render_proc render)
{
    HE_ASSERT(!is_valid(find(&render_graph->node_cache, HE_STRING(node_name))));
    
    Render_Graph_Node &node = append(&render_graph->nodes);

    node.name = HE_STRING(node_name);
    node.enabled = true;

    reset(&node.inputs);
    reset(&node.outputs);
    reset(&node.clear_values);
    
    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(&node - render_graph->nodes.data);
    insert(&render_graph->node_cache, HE_STRING(node_name), node_handle);

    if (!node.edges.data)
    {
        init(&node.edges);
    }

    node.before_render = before_render;
    node.render = render;

    node.render_pass = Resource_Pool< Render_Pass >::invalid_handle;
    node.shader = Resource_Pool< Shader >::invalid_handle;
    node.bind_group =  Resource_Pool< Bind_Group >::invalid_handle;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        node.frame_buffers[frame_index] = Resource_Pool< Frame_Buffer >::invalid_handle;
    }

    return node;
}

void set_shader(Render_Graph *render_graph, Render_Graph_Node_Handle node_handle, Shader_Handle shader, U32 bind_group_index)
{
    Render_Graph_Node *node = &render_graph->nodes[node_handle];

    Bind_Group_Descriptor pass_bind_group_descriptor =
    {
        .shader = shader,
        .group_index = bind_group_index,
    };

    node->shader = shader;
    node->bind_group = renderer_create_bind_group(pass_bind_group_descriptor);
}

void add_render_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Attachment_Operation op, Clear_Value clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    Render_Graph_Resource_Handle resource_handle = -1;

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(!is_valid(resource_it));
    HE_ASSERT(is_color_format(info.format));

    Render_Graph_Resource &resource = append(&render_graph->resources);
    resource.name = HE_STRING(resource_name);
    resource.node_handle = node_handle;
    resource.render_target_info = info;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.textures[frame_index] = Resource_Pool< Texture >::invalid_handle;
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.buffers[frame_index] = Resource_Pool< Buffer >::invalid_handle;
    }

    resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
    insert(&render_graph->resource_cache, resource.name, resource_handle);
    append(&node->outputs, { .resource_handle = resource_handle, .usage = RENDER_TARGET, .op = op, .clear_value = clear_value });
}

void add_storage_texture(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Clear_Value clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    Render_Graph_Resource_Handle resource_handle = -1;

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(!is_valid(resource_it));
    HE_ASSERT(is_color_format(info.format));

    Render_Graph_Resource &resource = append(&render_graph->resources);
    resource.name = HE_STRING(resource_name);
    resource.node_handle = node_handle;
    resource.render_target_info = info;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.textures[frame_index] = Resource_Pool< Texture >::invalid_handle;
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.buffers[frame_index] = Resource_Pool< Buffer >::invalid_handle;
    }

    resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
    insert(&render_graph->resource_cache, resource.name, resource_handle);
    append(&node->outputs, { .resource_handle = resource_handle, .usage = STORAGE_TEXTURE, .clear_value = clear_value });
}

void add_storage_buffer(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Buffer_Info info, U32 clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    Render_Graph_Resource_Handle resource_handle = -1;

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(!is_valid(resource_it));

    Render_Graph_Resource &resource = append(&render_graph->resources);
    resource.name = HE_STRING(resource_name);
    resource.node_handle = node_handle;
    resource.buffer_info = info;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.textures[frame_index] = Resource_Pool< Texture >::invalid_handle;
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.buffers[frame_index] = Resource_Pool< Buffer >::invalid_handle;
    }

    resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
    insert(&render_graph->resource_cache, resource.name, resource_handle);
    append(&node->outputs, { .resource_handle = resource_handle, .usage = STORAGE_BUFFER, .clear_value = { .ucolor = { clear_value, clear_value, clear_value, clear_value } } });
}

void add_render_target_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Attachment_Operation op, Clear_Value clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(is_valid(resource_it));

    Render_Graph_Node_Handle resource_handle = *resource_it.value;
    append(&node->inputs, { .resource_handle = resource_handle, .usage = RENDER_TARGET, .op = op, .clear_value = clear_value });
}

void add_texture_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name)
{
    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);
    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(is_valid(resource_it));

    Render_Graph_Node_Handle resource_handle = *resource_it.value;
    append(&node->inputs, { .resource_handle = resource_handle, .usage = SAMPLED_TEXTURE });
}

void add_storage_texture_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name)
{
    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);
    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(is_valid(resource_it));

    Render_Graph_Node_Handle resource_handle = *resource_it.value;
    append(&node->inputs, { .resource_handle = resource_handle, .usage = STORAGE_TEXTURE });
}

void add_storage_buffer_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name)
{
    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);
    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(is_valid(resource_it));

    Render_Graph_Node_Handle resource_handle = *resource_it.value;
    append(&node->inputs, { .resource_handle = resource_handle, .usage = STORAGE_BUFFER });
}

void add_depth_stencil_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Attachment_Operation op, Clear_Value clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    Render_Graph_Resource_Handle resource_handle = -1;

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(!is_valid(resource_it));
    HE_ASSERT(!is_color_format(info.format));

    Render_Graph_Resource &resource = append(&render_graph->resources);
    resource.name = HE_STRING(resource_name);
    resource.node_handle = node_handle;
    resource.render_target_info = info;

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.textures[frame_index] = Resource_Pool< Texture >::invalid_handle;
    }

    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
    {
        resource.buffers[frame_index] = Resource_Pool< Buffer >::invalid_handle;
    }

    resource_handle = (Render_Graph_Resource_Handle)(&resource - render_graph->resources.data);
    insert(&render_graph->resource_cache, resource.name, resource_handle);

    append(&node->outputs, { .resource_handle = resource_handle, .usage = RENDER_TARGET, .op = op, .clear_value = clear_value });
}

void set_depth_stencil_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Attachment_Operation op, Clear_Value clear_value)
{
    using enum Render_Graph_Resource_Usage;

    Render_Graph_Node_Handle node_handle = (Render_Graph_Node_Handle)(node - render_graph->nodes.data);

    auto resource_it = find(&render_graph->resource_cache, HE_STRING(resource_name));
    HE_ASSERT(is_valid(resource_it));

    Render_Graph_Node_Handle resource_handle = *resource_it.value;
    append(&node->inputs, { .resource_handle = resource_handle, .usage = RENDER_TARGET, .op = op, .clear_value = clear_value });
}

void set_presentable_attachment(Render_Graph *render_graph, const char *resource)
{
    auto it = find(&render_graph->resource_cache, HE_STRING(resource));
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

        for (U32 input_index = 0; input_index < node.inputs.count; input_index++)
        {
            Render_Graph_Node_Input &input = node.inputs[input_index];
            Render_Graph_Resource_Handle resource_handle = input.resource_handle;
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

        for (U32 output_index = 0; output_index < node.outputs.count; output_index++)
        {
            Render_Graph_Node_Output &output = node.outputs[output_index];
            Render_Graph_Resource_Handle resource_handle = output.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            output.resource_handle = resource_handle;

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
    U32 frame_index = renderer_state->current_frame_in_flight_index;

    auto &sorted_nodes = render_graph->topologically_sorted_nodes;

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Counted_Array< Update_Binding_Descriptor, 16 > bindings = {};
        Counted_Array< Clear_Value, HE_MAX_ATTACHMENT_COUNT > clear_values = {};

        Render_Graph_Node &node = render_graph->nodes[node_handle];

        for (U32 input_index = 0; input_index < node.inputs.count; input_index++)
        {
            Render_Graph_Node_Input &input = node.inputs[input_index];
            Render_Graph_Resource_Handle resource_handle = input.resource_handle;
            Render_Graph_Resource *resource = &render_graph->resources[resource_handle];

            switch (input.usage)
            {
                case Render_Graph_Resource_Usage::RENDER_TARGET:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::RENDER_TARGET);
                } break;

                case Render_Graph_Resource_Usage::SAMPLED_TEXTURE:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::SHADER_READ_ONLY);
                } break;

                case Render_Graph_Resource_Usage::STORAGE_TEXTURE:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::GENERAL);
                } break;

                case Render_Graph_Resource_Usage::STORAGE_BUFFER:
                {
                    Buffer_Handle buffer_handle = resource->buffers[frame_index];
                    renderer->invalidate_buffer(buffer_handle);
                } break;
            }

            if (input.usage == Render_Graph_Resource_Usage::SAMPLED_TEXTURE || input.usage == Render_Graph_Resource_Usage::STORAGE_TEXTURE)
            {
                U32 binding_numer = bindings.count;
                Update_Binding_Descriptor &binding = append(&bindings);
                binding.binding_number = binding_numer;
                binding.element_index = 0;
                binding.textures = &resource->textures[frame_index];
                binding.samplers = &renderer_state->default_texture_sampler;
                binding.count = 1;
            }
            else if (input.usage == Render_Graph_Resource_Usage::STORAGE_BUFFER)
            {
                U32 binding_numer = bindings.count;
                Update_Binding_Descriptor &binding = append(&bindings);
                binding.binding_number = binding_numer;
                binding.element_index = 0;
                binding.buffers = &resource->buffers[frame_index];
                binding.count = 1;
            }
        }

        for (U32 output_index = 0; output_index < node.outputs.count; output_index++)
        {
            Render_Graph_Node_Output &output = node.outputs[output_index];
            Render_Graph_Resource_Handle resource_handle = output.resource_handle;
            Render_Graph_Resource *resource = &render_graph->resources[resource_handle];

            switch (output.usage)
            {
                case Render_Graph_Resource_Usage::RENDER_TARGET:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::RENDER_TARGET);
                } break;

                case Render_Graph_Resource_Usage::SAMPLED_TEXTURE:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::SHADER_READ_ONLY);
                } break;

                case Render_Graph_Resource_Usage::STORAGE_TEXTURE:
                {
                    Texture_Handle texture_handle = resource->textures[frame_index];
                    renderer->change_texture_state(texture_handle, Resource_State::GENERAL);
                    renderer->clear_texture(texture_handle, output.clear_value);
                } break;

                case Render_Graph_Resource_Usage::STORAGE_BUFFER:
                {
                    Buffer_Handle buffer_handle = resource->buffers[frame_index];
                    renderer->fill_buffer(buffer_handle, output.clear_value.ucolor[0]);
                } break;
            }

            if (output.usage == Render_Graph_Resource_Usage::SAMPLED_TEXTURE || output.usage == Render_Graph_Resource_Usage::STORAGE_TEXTURE)
            {
                U32 binding_numer = bindings.count;
                Update_Binding_Descriptor &binding = append(&bindings);
                binding.binding_number = binding_numer;
                binding.element_index = 0;
                binding.textures = &resource->textures[frame_index];
                binding.samplers = &renderer_state->default_texture_sampler;
                binding.count = 1;
            }
            else if (output.usage == Render_Graph_Resource_Usage::STORAGE_BUFFER)
            {
                U32 binding_numer = bindings.count;
                Update_Binding_Descriptor &binding = append(&bindings);
                binding.binding_number = binding_numer;
                binding.element_index = 0;
                binding.buffers = &resource->buffers[frame_index];
                binding.count = 1;
            }
        }

        if (is_valid_handle(&renderer_state->bind_groups, node.bind_group))
        {
            renderer_update_bind_group(node.bind_group, to_array_view(bindings));
            Bind_Group *pass_bind_group = renderer_get_bind_group(node.bind_group);
            renderer->set_bind_groups(pass_bind_group->group_index, { .count = 1, .data = &node.bind_group });
        }

        Frame_Buffer *frame_buffer = renderer_get_frame_buffer(node.frame_buffers[frame_index]);
        renderer->set_viewport(frame_buffer->width, frame_buffer->height);

        if (node.before_render)
        {
            node.before_render(renderer, renderer_state);
        }

        renderer->begin_render_pass(node.render_pass, node.frame_buffers[frame_index], to_array_view(node.clear_values));
        node.render(renderer, renderer_state);
        renderer->end_render_pass(node.render_pass);
    }
}

void invalidate(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state)
{
    using enum Render_Graph_Resource_Usage;

    auto &sorted_nodes = render_graph->topologically_sorted_nodes;

    for (Render_Graph_Node_Handle node_handle : sorted_nodes)
    {
        Render_Graph_Node &node = render_graph->nodes[node_handle];

        for (const Render_Graph_Node_Output &output : node.outputs)
        {
            Render_Graph_Resource_Handle resource_handle = output.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];
            if (resource.node_handle == node_handle)
            {
                if (output.usage == Render_Graph_Resource_Usage::STORAGE_BUFFER)
                {
                    Buffer_Info &info = resource.buffer_info;

                    U64 size = info.size;

                    if (info.resizable)
                    {
                        U64 width = (U32)(renderer_state->back_buffer_width * info.scale_x);
                        U64 height = (U32)(renderer_state->back_buffer_height * info.scale_y);
                        size *= width * height;
                    }

                    Buffer_Descriptor buffer_descriptor =
                    {
                        .size = size,
                        .usage = info.usage
                    };

                    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                    {
                        if (is_valid_handle(&renderer_state->buffers, resource.buffers[frame_index]))
                        {
                            renderer_destroy_buffer(resource.buffers[frame_index]);
                        }
                        resource.buffers[frame_index] = renderer_create_buffer(buffer_descriptor);
                    }
                }
                else
                {
                    Render_Target_Info &info = resource.render_target_info;

                    if (info.resizable)
                    {
                        info.width = (U32)(renderer_state->back_buffer_width * info.scale_x);
                        info.height = (U32)(renderer_state->back_buffer_height * info.scale_y);
                    }

                    Texture_Descriptor texture_descriptor =
                    {
                        .width = info.width,
                        .height = info.height,
                        .format = info.format,
                        .sample_count = 1,
                        .is_attachment = output.usage == RENDER_TARGET,
                        .is_storage = output.usage == STORAGE_TEXTURE
                    };

                    for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                    {
                        if (is_valid_handle(&renderer_state->textures, resource.textures[frame_index]))
                        {
                            renderer_destroy_texture(resource.textures[frame_index]);
                        }
                        resource.textures[frame_index] = renderer_create_texture(texture_descriptor);
                    }
                }
            }
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

        for (U32 input_index = 0; input_index < node.inputs.count; input_index++)
        {
            Render_Graph_Node_Input &input = node.inputs[input_index];

            if (input.usage != RENDER_TARGET)
            {
                continue;
            }

            Render_Graph_Resource_Handle resource_handle = input.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.render_target_info.format;
            attachment_info.sample_count = 1;
            attachment_info.operation = input.op;

            width = resource.render_target_info.width;
            height = resource.render_target_info.height;

            if (is_color_format(resource.render_target_info.format))
            {
                append(&render_pass_descriptor.color_attachments, attachment_info);
                append(&node.clear_values, input.clear_value);

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.textures[frame_index]);
                }
            }
        }

        for (U32 ouput_index = 0; ouput_index < node.outputs.count; ouput_index++)
        {
            Render_Graph_Node_Output &output = node.outputs[ouput_index];

            if (output.usage != RENDER_TARGET)
            {
                continue;
            }

            Render_Graph_Resource_Handle resource_handle = output.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.render_target_info.format;
            attachment_info.sample_count = 1;
            attachment_info.operation = output.op;

            width = resource.render_target_info.width;
            height = resource.render_target_info.height;

            if (is_color_format(resource.render_target_info.format))
            {
                append(&render_pass_descriptor.color_attachments, attachment_info);
                append(&node.clear_values, output.clear_value);

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.textures[frame_index]);
                }
            }
        }

        for (U32 input_index = 0; input_index < node.inputs.count; input_index++)
        {
            Render_Graph_Node_Input &input = node.inputs[input_index];

            if (input.usage != RENDER_TARGET)
            {
                continue;
            }

            Render_Graph_Resource_Handle resource_handle = input.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.render_target_info.format;
            attachment_info.sample_count = 1;
            attachment_info.operation = input.op;

            width = resource.render_target_info.width;
            height = resource.render_target_info.height;

            if (!is_color_format(resource.render_target_info.format))
            {
                append(&render_pass_descriptor.depth_stencil_attachments, attachment_info);
                append(&node.clear_values, input.clear_value);

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.textures[frame_index]);
                }
            }
        }

        for (U32 ouput_index = 0; ouput_index < node.outputs.count; ouput_index++)
        {
            Render_Graph_Node_Output &output = node.outputs[ouput_index];

            if (output.usage != RENDER_TARGET)
            {
                continue;
            }

            Render_Graph_Resource_Handle resource_handle = output.resource_handle;
            Render_Graph_Resource &resource = render_graph->resources[resource_handle];

            Attachment_Info attachment_info = {};
            attachment_info.format = resource.render_target_info.format;
            attachment_info.sample_count = 1;
            attachment_info.operation = output.op;

            width = resource.render_target_info.width;
            height = resource.render_target_info.height;

            if (!is_color_format(resource.render_target_info.format))
            {
                append(&render_pass_descriptor.depth_stencil_attachments, attachment_info);
                append(&node.clear_values, output.clear_value);

                for (U32 frame_index = 0; frame_index < HE_MAX_FRAMES_IN_FLIGHT; frame_index++)
                {
                    append(&frame_buffer_descriptors[frame_index].attachments, resource.textures[frame_index]);
                }
            }
        }

        if (is_valid_handle(&renderer_state->render_passes, node.render_pass))
        {
            renderer->destroy_render_pass(node.render_pass, true);
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
                renderer->destroy_frame_buffer(node.frame_buffers[frame_index], true);
                renderer->create_frame_buffer(node.frame_buffers[frame_index], frame_buffer_descriptors[frame_index]);
            }
            else
            {
                node.frame_buffers[frame_index] = renderer_create_frame_buffer(frame_buffer_descriptors[frame_index]);
            }
        }
    }
}

Texture_Handle get_presentable_attachment(Render_Graph *render_graph, Renderer_State *renderer_state)
{
    return render_graph->presentable_resource->textures[renderer_state->current_frame_in_flight_index];
}

Texture_Handle get_texture_resource(Render_Graph *render_graph, Renderer_State *renderer_state, String name)
{
    auto it = find(&render_graph->resource_cache, name);
    if (!is_valid(it))
    {
        return Resource_Pool< Texture >::invalid_handle;
    }
    const Render_Graph_Resource &resource = render_graph->resources[*it.value];
    Texture_Handle result = resource.textures[renderer_state->current_frame_in_flight_index];
    return result;
}