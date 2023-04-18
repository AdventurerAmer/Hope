#include "core/defines.h"
#include "core/engine.h"
#include "core/input_codes.h"

extern "C" bool
init_game(Engine *engine)
{
	Platform_API *platform = &engine->platform_api;
	platform->debug_printf("init_game\n");
    return true;
}

extern "C" void
on_event(Engine *engine, Event event)
{
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

extern "C" void
on_update(Engine *engine, F32 delta_time)
{
	(void)engine;
	(void)delta_time;
	Platform_API *platform = &engine->platform_api;
	U32 x = 10;
	platform->debug_printf("hello, sailor my friend, %d\n", x);
}