#pragma once

#include <assets/asset_manager.h>
#include <rendering/renderer_types.h>

namespace Inspector_Panel
{

void draw();
void inspect_scene_node(Asset_Handle scene_asset, S32 scene_node_index);
void inspect_asset(Asset_Handle asset_handle);

} // // namespace Inspector_Panel