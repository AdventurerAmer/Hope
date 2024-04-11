#pragma once

#include "assets/asset_manager.h"

Load_Asset_Result load_scene(String path, const Embeded_Asset_Params *params = nullptr);
void unload_scene(Load_Asset_Result load_result);