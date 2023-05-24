#pragma once

#include "defines.h"

#if HE_OS_WINDOWS
#include "platform/win32_input_codes.h"
#endif

enum Input_State : U8
{
    InputState_Released = 0,
    InputState_Pressed = 1,
    InputState_Held = 2,
};

#define MAX_KEY_STATES 1024
#define MAX_BUTTON_STATES 64

struct Input
{
    Input_State key_states[MAX_KEY_STATES];
    Input_State button_states[MAX_BUTTON_STATES];

    U16 prev_mouse_x;
    U16 prev_mouse_y;

    U16 mouse_x;
    U16 mouse_y;

    S32 mouse_delta_x;
    S32 mouse_delta_y;
};

bool init_input(Input *input);