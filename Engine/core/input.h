#pragma once

#include "defines.h"

#if HE_OS_WINDOWS
#include "platform/win32_input_codes.h"
#endif

enum class Input_State : U8
{
    RELEASED,
    PRESSED,
    HELD,
};

#define HE_MAX_KEY_STATES 1024
#define HE_MAX_BUTTON_STATES 64

struct Input
{
    Input_State key_states[HE_MAX_KEY_STATES];
    Input_State button_states[HE_MAX_BUTTON_STATES];

    U16 prev_mouse_x;
    U16 prev_mouse_y;

    U16 mouse_x;
    U16 mouse_y;

    S32 mouse_delta_x;
    S32 mouse_delta_y;
};

bool init_input(Input *input);