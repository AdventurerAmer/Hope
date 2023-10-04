#pragma once

#include "core/defines.h"
#include "core/memory.h"

#include "rendering/renderer_types.h"

#include "containers/string.h"
#include "containers/array.h"
#include "containers/dynamic_array.h"
#include "containers/hash_map.h"

#include <functional>

#define HE_MAX_RENDER_GRAPH_NODE_COUNT 1024
#define HE_MAX_RENDER_GRAPH_RESOURCE_COUNT 1024

typedef S32 Render_Graph_Node_Handle;
typedef S32 Render_Graph_Resource_Handle;

enum struct Render_Graph_Resource_Type : U8
{
    BUFFER,
    TEXTURE,
    ATTACHMENT,
    REFERENCE
}; 

union Render_Graph_Resource_Info
{
    struct
    {
        Buffer_Handle handle;
    } buffer;

    struct
    {
        U32 width;
        U32 height;
        Texture_Format format;
        Attachment_Operation operation;
        U32 sample_count;
        Texture_Handle handles[HE_MAX_FRAMES_IN_FLIGHT];
    } texture;
};

struct Render_Graph_Node_Input
{
    String name;
    Render_Graph_Resource_Type type;
};

struct Render_Graph_Node_Output
{
    String name;
    Render_Graph_Resource_Type type;
    Render_Graph_Resource_Info info;
};

struct Render_Graph_Resource
{
    String name;
    Render_Graph_Resource_Type type;
    Render_Graph_Resource_Info info;

    Render_Graph_Node_Handle node_handle;
    Render_Graph_Resource_Handle output_handle;
    
    U32 ref_count;
};

typedef std::function< Array< Clear_Value, HE_MAX_ATTACHMENT_COUNT >(struct Renderer *renderer, struct Renderer_State *renderer_state) > pre_render_pass_proc;
typedef std::function< void(struct Renderer *renderer, struct Renderer_State *renderer_state) > render_pass_proc;

struct Render_Graph_Node
{
    String name;

    Render_Pass_Handle render_pass;
    Frame_Buffer_Handle frame_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Array< Render_Graph_Resource_Handle, HE_MAX_ATTACHMENT_COUNT > inputs;
    Array< Render_Graph_Resource_Handle, HE_MAX_ATTACHMENT_COUNT > outputs;

    Dynamic_Array< Render_Graph_Node_Handle > edges;

    bool enabled;

    pre_render_pass_proc pre_render;
    render_pass_proc render; 
};

struct Render_Graph
{
    Allocator allocator;

    Hash_Map< String, Render_Graph_Resource_Handle > resource_cache;

    Array< Render_Graph_Resource, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT > resources;
    Array< Render_Graph_Node, HE_MAX_RENDER_GRAPH_NODE_COUNT > nodes;

    Array< U8, HE_MAX_RENDER_GRAPH_NODE_COUNT > visited;
    Array< Render_Graph_Node_Handle, HE_MAX_RENDER_GRAPH_NODE_COUNT > node_stack;
    Array< Render_Graph_Node_Handle, HE_MAX_RENDER_GRAPH_NODE_COUNT > topologically_sorted_nodes;
    
    Array< Texture_Handle, HE_MAX_RENDER_GRAPH_RESOURCE_COUNT > texture_free_list;
};

void init(Render_Graph *render_graph, Allocator allocator);

void add_node(Render_Graph *render_graph, const String &name, const Array_View< Render_Graph_Node_Input > &inputs, const Array_View< Render_Graph_Node_Output > &outputs, pre_render_pass_proc pre_render, render_pass_proc render);
void compile(Render_Graph *render_graph);

void render(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);