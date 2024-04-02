#pragma once

#include <assets/asset_manager.h>

struct Scene_Node;

namespace Inspector_Panel
{

void draw();
void inspect(Scene_Node *scene_node);
void inspect(Asset_Handle asset_handle);

} // // namespace Inspector_Panel