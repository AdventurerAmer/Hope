#include "core/defines.h"
#include "core/engine.h"
#include "core/input_codes.h"

extern "C" bool init_game(Engine *engine)
{
	Platform *platform = &engine->platform;
	platform->debug_printf("init_game\n");
    return true;
}

extern "C" void on_event(Engine *engine, Event event)
{
	Platform *platform = &engine->platform;
	switch (event.type)
	{
		case EventType_Key:
		{
			if (event.pressed)
			{
				if (event.key == HE_KEY_ESCAPE)
				{
					engine->is_running = false;
				}
			}
		} break;
	}
}

extern "C" void on_update(Engine *engine, F32 delta_time)
{
	Platform *platform = &engine->platform;
}