#pragma once

#include "core/defines.h"
#include "core/memory.h"

#include "rendering/renderer_types.h"

#include "containers/string.h"
#include "containers/array.h"
#include "containers/counted_array.h"
#include "containers/dynamic_array.h"
#include "containers/hash_map.h"

#include <functional>

#define HE_MAX_RENDER_GRAPH_NODE_COUNT 1024
#define HE_MAX_RENDER_GRAPH_RESOURCE_COUNT 1024

typedef S32 Render_Graph_Node_Handle;
typedef S32 Render_Graph_Resource_Handle;

struct Render_Graph_Resource_Info
{
    Texture_Format format;

    bool resizable_sample = false;
    U32 sample_count = 1;
    
    U32 width = 0;
    U32 height = 0;

    bool resizable = false;
    F32 scale_x = 1.0f;
    F32 scale_y = 1.0f;

    Texture_Handle handles[HE_MAX_FRAMES_IN_FLIGHT] = 
    { 
        Resource_Pool< Texture >::invalid_handle,
        Resource_Pool< Texture >::invalid_handle,
        Resource_Pool< Texture >::invalid_handle
    };
};

struct Render_Target_Info
{
    const char *name;
    Attachment_Operation operation = Attachment_Operation::CLEAR;
    Render_Graph_Resource_Info info;
};

struct Render_Graph_Resource
{
    String name;
    Render_Graph_Resource_Info info;

    Render_Graph_Node_Handle node_handle;

    Render_Graph_Resource_Handle resolver_handle;

    U32 ref_count;
};

typedef std::function< void(struct Renderer *renderer, struct Renderer_State *renderer_state) > render_proc;


struct Render_Graph_Node
{
    String name;

    bool enabled;

    Counted_Array< Clear_Value, HE_MAX_ATTACHMENT_COUNT > clear_values;

    Render_Pass_Handle render_pass;
    Frame_Buffer_Handle frame_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Counted_Array< Render_Graph_Resource_Handle, HE_MAX_ATTACHMENT_COUNT > original_render_targets;
    Counted_Array< Render_Graph_Resource_Handle, HE_MAX_ATTACHMENT_COUNT > render_targets;
    Counted_Array< Attachment_Operation, HE_MAX_ATTACHMENT_COUNT > render_target_operations;

    Counted_Array< Render_Graph_Resource_Handle, HE_MAX_ATTACHMENT_COUNT > resolve_render_targets;

    Dynamic_Array< Render_Graph_Node_Handle > edges;

    render_proc render;
    render_proc before;
    render_proc after;
};

struct Render_Graph
{
    Hash_Map< String, Render_Graph_Node_Handle > node_cache;
    Counted_Array< Render_Graph_Node, HE_MAX_RENDER_GRAPH_NODE_COUNT > nodes;

    Hash_Map< String, Render_Graph_Resource_Handle > resource_cache;
    Counted_Array< Render_Graph_Resource, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT > resources;

    Counted_Array< U8, HE_MAX_RENDER_GRAPH_NODE_COUNT > visited;
    Counted_Array< Render_Graph_Node_Handle, HE_MAX_RENDER_GRAPH_NODE_COUNT > node_stack;
    Counted_Array< Render_Graph_Node_Handle, HE_MAX_RENDER_GRAPH_NODE_COUNT > topologically_sorted_nodes;
    
    Counted_Array< Texture_Handle, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT > texture_free_list;

    Render_Graph_Resource *presentable_resource;
};

void init(Render_Graph *render_graph);
void reset(Render_Graph *render_graph);

Render_Graph_Node& add_node(Render_Graph *render_graph, const char *name, const Array_View< Render_Target_Info > &render_targets, render_proc render, render_proc before = nullptr, render_proc after = nullptr);
void add_resolve_color_attachment(Render_Graph *render_graph, Render_Graph_Node *node, const char *render_target, const char *resolve_render_target);

void set_presentable_attachment(Render_Graph *render_graph, const char *render_target);

bool compile(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);
void invalidate(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);
void render(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);
Texture_Handle get_presentable_attachment(Render_Graph *render_graph, struct Renderer_State *renderer_state);
Texture_Handle get_texture_resource(Render_Graph *render_graph, struct Renderer_State *renderer_state, String name);

Render_Graph_Node_Handle get_node(Render_Graph *render_graph, String name);
Render_Pass_Handle get_render_pass(Render_Graph *render_graph, String name);