#pragma once

#include <assets/asset_manager.h>
#include <rendering/renderer_types.h>

struct Scene;

namespace Inspector_Panel
{

void draw();
void inspect(Scene_Handle scene_handle, S32 scene_node_index);
void inspect(Asset_Handle asset_handle);

} // // namespace Inspector_Panel