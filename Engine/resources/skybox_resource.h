#pragma once

#include "core/defines.h"
#include "rendering/renderer_types.h"

#pragma pack(push, 1)

struct Skybox_Resource_Info
{
    glm::vec3 tint_color;
    U64 texture_resources[6];
};

#pragma pack(pop)

bool condition_skybox_to_resource(struct Read_Entire_File_Result *asset_file_result, struct Asset *asset, struct Resource *resource, struct Memory_Arena *arena);
bool load_skybox_resource(struct Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena);
void unload_skybox_resource(Resource *resource);