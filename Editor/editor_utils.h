#pragma once

#include <containers/string.h>

struct Asset_Handle;

struct Select_Asset_Config
{
    bool nullify = true;
    bool load_on_select = false;
};

bool select_asset(String name, String type, Asset_Handle *asset_handle, const Select_Asset_Config &config = Select_Asset_Config());