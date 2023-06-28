#include "input.h"
#include "memory.h"

bool init_input(Input *input)
{
    zero_memory(input->key_states, sizeof(Input_State) * MAX_KEY_STATES);
    zero_memory(input->button_states, sizeof(Input_State) * MAX_BUTTON_STATES);

    input->prev_mouse_x = 0;
    input->prev_mouse_y = 0;
    input->mouse_x = 0;
    input->mouse_y = 0;

    return true;
}