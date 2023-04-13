#include "engine.h"
#include "platform.h"

internal_function bool
startup(Engine *engine, const Engine_Configuration &configuration)
{
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    U64 channel_mask = 0xFFFFFFFFFFFFFFFF;
    bool logger_initied = init_logger(&debug_state.main_logger, "all.log",
                                      Verbosity_Trace, channel_mask);
    if (!logger_initied)
    {
        return false;
    }

    Mem_Size required_memory_size =
        configuration.permanent_memory_size + configuration.transient_memory_size;

    void *memory = platform_allocate_memory(required_memory_size);
    if (!memory)
    {
        return false;
    }

    U8 *permanent_memory = (U8 *)memory;
    engine->permanent_memory_size = configuration.permanent_memory_size;
    engine->permanent_arena = create_memory_arena(permanent_memory,
                                                 configuration.permanent_memory_size);

    U8 *transient_memory = (U8 *)memory + configuration.permanent_memory_size;
    engine->transient_memory_size = configuration.transient_memory_size;
    engine->transient_arena = create_memory_arena(transient_memory,
                                                 configuration.transient_memory_size);

    return true;
}

internal_function void
shutdown(Engine *engine)
{
    // todo(amer): logging should only be enabled in non-shipping builds
    Logger *logger = &debug_state.main_logger;
    deinit_logger(logger);
}