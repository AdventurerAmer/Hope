#pragma once

#include "core/defines.h"
#include "containers/string.h"
#include "assets/asset_manager.h"

void on_import_model(Asset_Handle asset_handle);

Load_Asset_Result load_model(String path, const Embeded_Asset_Params *params);
void unload_model(Load_Asset_Result load_result);

Load_Asset_Result load_static_mesh(String path, const Embeded_Asset_Params *params = nullptr);
void unload_static_mesh(Load_Asset_Result load_result);