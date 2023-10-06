#include "renderer_utils.h"


bool is_color_format(Texture_Format format)
{
    switch (format)
    {
        case Texture_Format::R8G8B8A8_SRGB:
        case Texture_Format::B8G8R8A8_SRGB:
        {
            return true;
        } break;
    }

    return false;
}