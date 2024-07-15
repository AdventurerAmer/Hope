#type compute

#version 450

#extension GL_GOOGLE_include_directive : enable

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout (set = 0, binding = 0, rgba8) uniform image2D tex0;

void main()
{
    vec4 value = vec4(0.0, 0.0, 0.0, 1.0);
    ivec2 texel_coord = ivec2(gl_GlobalInvocationID.xy);

    vec4 old_value = imageLoad(tex0, texel_coord).rgba;

    if (old_value.r == 0.0f)
    {
        value = vec4(1.0, 0.0, 0.0, 1.0);
    }
    else
    {
        value.x = float(texel_coord.x)/(gl_NumWorkGroups.x);
        value.y = float(texel_coord.y)/(gl_NumWorkGroups.y);
    }

    imageStore(tex0, texel_coord, value);
}