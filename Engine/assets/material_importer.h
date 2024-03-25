#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "assets/asset_manager.h"

Load_Asset_Result load_material(String path);
void unload_material(Load_Asset_Result load_result);