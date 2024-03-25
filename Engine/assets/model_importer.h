#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "assets/asset_manager.h"

Load_Asset_Result load_model(String path);
void unload_model(Load_Asset_Result load_result);