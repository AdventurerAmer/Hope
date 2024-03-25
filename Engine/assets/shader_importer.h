#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "assets/asset_manager.h"

Load_Asset_Result load_shader(String path);
void unload_shader(Load_Asset_Result load_result);