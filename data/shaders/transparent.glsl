#type vertex

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../shaders/common.glsl"

layout (location = 0) in vec3 in_position;

layout (std430, set = SHADER_GLOBALS_BIND_GROUP, binding = SHADER_INSTANCE_STORAGE_BUFFER_BINDING) readonly buffer Instance_Buffer
{
    Instance_Data instances[];
};

void main()
{
    float noop = NOOP(in_position.x) * NOOP(float(instances[gl_InstanceIndex].entity_index)) * NOOP(globals.gamma);

    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f + -1.0f, 0.0f, 1.0f) * noop;
}

#type fragment

#version 450

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "../shaders/common.glsl"

layout(set = SHADER_PASS_BIND_GROUP, binding = SHADER_BINDLESS_TEXTURES_BINDING) uniform sampler2D u_textures[];

vec4 sample_texture(uint texture_index, vec2 uv)
{
    return texture( u_textures[ nonuniformEXT( texture_index ) ], uv );
}

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_STORAGE_BUFFER_BINDING) readonly buffer Light_Buffer
{
    Light lights[];
};

layout (std430, set = SHADER_PASS_BIND_GROUP, binding = SHADER_LIGHT_BINS_STORAGE_BUFFER_BINDING) readonly buffer Light_Bins_Buffer
{
    uint light_bins[];
};

layout (set = 2, binding = 0) uniform sampler2D rt0;

layout (set = 2, binding = 1, r32ui) uniform uimage2D head_index_image;

layout (std430, set = 2, binding = 2) readonly buffer Node_Buffer
{
    Node nodes[];
};

layout (std430, set = 2, binding = 3) readonly buffer Node_Count_Buffer
{
    int node_count;
};

layout(location = 0) out int out_entity_index;
layout(location = 1) out vec4 out_color;

#define MAX_FRAGMENT_COUNT 128

void main()
{
    Node fragments[MAX_FRAGMENT_COUNT];
    int count = 0;

    ivec2 coords = ivec2(gl_FragCoord.xy);
    uint node_index = imageLoad(head_index_image, coords).r;

    while (node_index != 0xffffffff && count < MAX_FRAGMENT_COUNT)
    {
        fragments[count] = nodes[node_index];
        node_index = fragments[count].next;
        ++count;
    }

    for (uint i = 1; i < count; ++i)
    {
        Node insert = fragments[i];
        uint j = i;
        while (j > 0 && insert.depth > fragments[j - 1].depth)
        {
            fragments[j] = fragments[j - 1];
            --j;
        }
        fragments[j] = insert;
    }

    vec4 color = texelFetch(rt0, coords, 0);

    for (int i = 1; i < count; ++i)
    {
        color = mix(color, fragments[i].color, fragments[i].color.a);
    }

    float noop = NOOP(lights[0].color.r) * NOOP(float(light_bins[0])) * NOOP(float(node_count));
    vec3 final_color = color.rgb;
    final_color = final_color / (final_color + vec3(1.0));
    final_color = linear_to_srgb(final_color, globals.gamma);
    out_color = vec4(final_color, 1.0);
    int mask0 = int(step(0.0, count));
    int mask1 = int(step(1.0, count));
    out_entity_index = fragments[count - 1].entity_index;
}