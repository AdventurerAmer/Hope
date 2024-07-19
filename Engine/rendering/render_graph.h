#pragma once

#include "core/defines.h"
#include "core/memory.h"
#include "core/job_system.h"

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

enum class Render_Graph_Node_Type
{
    GRAPHICS,
    COMPUTE
};

struct Render_Target_Info
{
    Texture_Format format;

    U32 width  = 0;
    U32 height = 0;

    bool resizable = true;
    F32 scale_x = 1.0f;
    F32 scale_y = 1.0f;
};

struct Buffer_Info
{
    U32 size = 1;
    Buffer_Usage usage = Buffer_Usage::STORAGE_GPU_SIDE;
    bool resizable = false;
    F32 scale_x = 1.0f;
    F32 scale_y = 1.0f;
};

struct Render_Graph_Resource
{
    String name;

    Render_Target_Info render_target_info;
    Texture_Handle textures[HE_MAX_FRAMES_IN_FLIGHT];

    Buffer_Info buffer_info;
    Buffer_Handle buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Render_Graph_Node_Handle node_handle;
};

typedef std::function< void(struct Renderer *renderer, struct Renderer_State *renderer_state) > Execute_Render_Graph_Node_Proc;

enum Render_Graph_Resource_Usage
{
    RENDER_TARGET,
    SAMPLED_TEXTURE,
    STORAGE_TEXTURE,
    STORAGE_BUFFER
};

struct Render_Graph_Node_Input
{
    Render_Graph_Resource_Handle resource_handle;
    Render_Graph_Resource_Usage usage;
    Attachment_Operation op;
    Clear_Value clear_value;
};

struct Render_Graph_Node_Output
{
    Render_Graph_Resource_Handle resource_handle;
    Render_Graph_Resource_Usage usage;
    Attachment_Operation op;
    Clear_Value clear_value;
};

struct Render_Graph_Node
{
    String name;
    Render_Graph_Node_Type type;

    bool enabled;

    Counted_Array< Render_Graph_Node_Input, HE_MAX_ATTACHMENT_COUNT > inputs;
    Counted_Array< Render_Graph_Node_Output, HE_MAX_ATTACHMENT_COUNT > outputs;

    Counted_Array< Clear_Value, HE_MAX_ATTACHMENT_COUNT > clear_values;

    Dynamic_Array< Render_Graph_Node_Handle > edges;

    Execute_Render_Graph_Node_Proc execute;

    Shader_Handle shader;
    Bind_Group_Handle bind_group;

    Render_Pass_Handle render_pass;
    Frame_Buffer_Handle frame_buffers[HE_MAX_FRAMES_IN_FLIGHT];

    Job_Handle job_handle;
    Command_List command_list;
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

    Render_Graph_Resource *presentable_resource;
};

void init(Render_Graph *render_graph);

Render_Graph_Node& add_graphics_node(Render_Graph *render_graph, const char *name, Execute_Render_Graph_Node_Proc execute);
Render_Graph_Node& add_compute_node(Render_Graph *render_graph, const char *name, Execute_Render_Graph_Node_Proc execute);

void set_shader(Render_Graph *render_graph, Render_Graph_Node_Handle node_handle, Shader_Handle shader, U32 bind_group_index = 2);

void add_render_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Attachment_Operation op, Clear_Value clear_value = {});

void add_storage_texture(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Clear_Value clear_value = {});
void add_storage_buffer(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Buffer_Info buffer_info, U32 clear_value = 0);

void add_render_target_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Attachment_Operation op, Clear_Value clear_value = {});
void add_texture_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name);
void add_storage_texture_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name);
void add_storage_buffer_input(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name);

void add_depth_stencil_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Render_Target_Info info, Attachment_Operation op, Clear_Value clear_value = {});

void set_depth_stencil_target(Render_Graph *render_graph, Render_Graph_Node *node, const char *resource_name, Attachment_Operation op, Clear_Value clear_value = {});

void set_presentable_attachment(Render_Graph *render_graph, const char *render_target);

bool compile(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);
void invalidate(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);
void render(Render_Graph *render_graph, struct Renderer *renderer, struct Renderer_State *renderer_state);

Texture_Handle get_presentable_attachment(Render_Graph *render_graph, struct Renderer_State *renderer_state);
Texture_Handle get_texture_resource(Render_Graph *render_graph, struct Renderer_State *renderer_state, String name);

Render_Graph_Node_Handle get_node(Render_Graph *render_graph, String name);
Render_Pass_Handle get_render_pass(Render_Graph *render_graph, String name);