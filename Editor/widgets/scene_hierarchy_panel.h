#pragma once

#include <core/defines.h>

namespace Scene_Hierarchy_Panel {

void draw(U64 scene_asset_uuid);
void select(S32 node_index);
void reset_selection();

}