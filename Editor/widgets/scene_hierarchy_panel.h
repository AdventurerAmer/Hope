#pragma once

#include <core/defines.h>

struct Scene;

namespace Scene_Hierarchy_Panel {

void draw(U64 scene_asset_uuid);
void select(S32 node_index);
S32 get_selected_node();
void reset_selection();
void new_node(Scene *scene, U32 parent_index = 0);
void rename_node(Scene *scene, U32 node_index);
void delete_node(Scene *scene, U32 node_index);
void duplicate_node(Scene *scene, U32 node_index);

}