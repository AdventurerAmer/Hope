#pragma once

#include "core/defines.h"
#include "rendering/renderer_types.h"

#pragma pack(push, 1)

struct Texture_Resource_Info
{
    U32 width;
    U32 height;
    Texture_Format format;
    bool mipmapping;
    U64 data_offset;
};

#pragma pack(pop)

bool condition_texture_to_resource(struct Read_Entire_File_Result *asset_file_result, struct Asset *asset, struct Resource *resource, struct Memory_Arena *arena);
bool load_texture_resource(struct Open_File_Result *open_file_result, Resource *resource, Memory_Arena *arena);
void unload_texture_resource(Resource *resource);