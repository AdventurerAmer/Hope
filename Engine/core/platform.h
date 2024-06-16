#pragma once

#include "defines.h"

enum class Event_Type : U8
{
    KEY,
    MOUSE,
    RESIZE,
    CLOSE
};

struct Event
{
    Event_Type type;

    union
    {
        U16 key;
        U16 button;
    };

    bool pressed;
    bool held;

    bool is_shift_down;
    bool is_control_down;
    bool is_alt_down;
    bool double_click;
    
    S16 mouse_x;
    S16 mouse_y;

    bool mouse_wheel_up;
    bool mouse_wheel_down;

    bool minimized;
    bool maximized;
    bool restored;

    U16 client_width;
    U16 client_height;

    U16 window_width;
    U16 window_height;
};

//
// memory
//

U64 platform_get_total_memory_size();
void* platform_allocate_memory(U64 size);
void* platform_reserve_memory(U64 size);
bool platform_commit_memory(void *memory, U64 size);
void platform_deallocate_memory(void *memory);

//
// window
//

enum class Window_Mode : U8
{
    WINDOWED   = 0,
    FULLSCREEN = 1
};

struct Window
{
    U32 width;
    U32 height;
    const char *title;
    Window_Mode mode;
    void *platform_window_state;
    bool maximized;
};

bool platform_create_window(Window *window, const char *title, U32 width, U32 height, bool maximized = false, Window_Mode window_mode = Window_Mode::WINDOWED);
void platform_set_window_mode(Window *window, Window_Mode window_mode);
bool platform_open_file_dialog(char *buffer, U64 count, const char *title, U64 title_count, const char *filter, U64 filter_count, const char **extensions, U32 extension_count);
bool platform_save_file_dialog(char *buffer, U64 count, const char *title, U64 title_count, const char *filter, U64 filter_count, const char **extensions, U32 extension_count);

//
// files
//

bool platform_path_exists(const char *path, bool *is_file = nullptr);
U64 platform_get_file_last_write_time(const char *path);
bool platform_get_current_working_directory(char *buffer, U64 *count);

typedef void(*on_walk_directory_proc)(struct String *path, bool is_directory);
void platform_walk_directory(const char *path, bool recursive, on_walk_directory_proc on_walk_directory_proc);

enum Open_File_Flags : U8
{
    OpenFileFlag_Read     = 1 << 0,
    OpenFileFlag_Write    = 1 << 1,
    OpenFileFlag_Truncate = 1 << 2,
};

struct Open_File_Result
{
    void *handle;
    U64 size;
    bool success;
};

Open_File_Result platform_open_file(const char *filepath, Open_File_Flags open_file_flags);

bool platform_read_data_from_file(const Open_File_Result *open_file_result, U64 offset, void *data, U64 size);

bool platform_write_data_to_file(const Open_File_Result *open_file_result, U64 offset, void *data, U64 size);

bool platform_close_file(Open_File_Result *open_file_result);

enum class Watch_Directory_Result
{
    FILE_CREATED,
    FILE_RENAMED,
    FILE_MODIFIED,
    FILE_DELETED
};

typedef void(*on_watch_directory_proc)(Watch_Directory_Result result, String old_path, String new_path);

bool platform_watch_directory(const char *path, on_watch_directory_proc on_watch_directory);

//
// dynamic library
//

struct Dynamic_Library
{
    void *platform_dynamic_library_state;
};

bool platform_load_dynamic_library(Dynamic_Library *dynamic_library, const char *filepath);
void *platform_get_proc_address(Dynamic_Library *dynamic_library, const char *proc_name);
bool platform_unload_dynamic_library(Dynamic_Library *dynamic_library);

//
// vulkan
//

void* platform_create_vulkan_surface(struct Engine *engine,
                                     void *instance,
                                     const void *allocator_callbacks = nullptr);
//
// threading
//

// todo(amer): get ride of unsigned long
typedef unsigned long (*Thread_Proc)(void *params);

struct Thread
{
    void *platform_thread_state;
};

bool platform_create_and_start_thread(Thread *thread, Thread_Proc thread_proc, void *params, const char *name = nullptr);
U32 platform_get_thread_count();
U32 platform_get_current_thread_id();
U32 platform_get_thread_id(Thread *thread);

struct Mutex
{
    void *platform_mutex_state;
};

bool platform_create_mutex(Mutex *mutex);
void platform_lock_mutex(Mutex *mutex);
void platform_unlock_mutex(Mutex *mutex);
void platform_wait_for_mutexes(Mutex *mutexes, U32 mutex_count);

struct Semaphore
{
    void *platform_semaphore_state;
};

bool platform_create_semaphore(Semaphore *semaphore, U32 init_count = 0);
bool signal_semaphore(Semaphore *semaphore, U32 increase_amount = 1);
bool wait_for_semaphore(Semaphore *semaphore);

//
// imgui
//

void platform_init_imgui(struct Engine *engine);
void platform_imgui_new_frame();
void platform_shutdown_imgui();

//
// debugging
//

void platform_debug_printf(const char *message);

//
// misc
//

bool platform_execute_command(const char *command);