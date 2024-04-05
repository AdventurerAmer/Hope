#pragma once

#include "assets/asset_manager.h"

Load_Asset_Result load_texture(String path, const Embeded_Asset_Params *params = nullptr);
void unload_texture(Load_Asset_Result load_result);