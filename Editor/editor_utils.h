#pragma once

#include <containers/string.h>

struct Asset_Handle;

bool select_asset(String name, String type, Asset_Handle *asset_handle);