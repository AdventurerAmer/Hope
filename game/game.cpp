#include "core/defines.h"
#include "core/engine.h"
#include "core/input_codes.h"

extern "C" bool
init_game(Engine *engine)
{
	Platform_API *platform = &engine->platform_api;
    (void)platform;
    return true;
}

extern "C" void
on_event(Engine *engine, Event event)
{
	Platform_API *platform = &engine->platform_api;

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
				else if (event.key == HE_KEY_F11)
				{
					platform->toggle_fullscreen(engine);
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
	(void)platform;
}