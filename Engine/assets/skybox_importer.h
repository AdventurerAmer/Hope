#pragma once

#include "assets/asset_manager.h"

Load_Asset_Result load_skybox(String path, const Embeded_Asset_Params *params);
void unload_skybox(Load_Asset_Result load_result);