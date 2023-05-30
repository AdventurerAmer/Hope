#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN 1
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#pragma warning(push, 0)
#include <strsafe.h>
#include <windows.h>
#pragma warning(pop)

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "core/platform.h"
#include "core/memory.h"
#include "core/engine.h"

#include "core/debugging.h"

// todo(amer): move to static config...
#ifndef HE_APP_NAME
#define HE_APP_NAME "Hope"
#endif

#define HE_WINDOW_CLASS_NAME HE_APP_NAME "_WindowClass"

struct Win32_State
{
    HWND window;
    HINSTANCE instance;
    U32 window_width;
    U32 window_height;
    U32 window_client_width;
    U32 window_client_height;
    S32 mouse_wheel_accumulated_delta;
    HCURSOR cursor;
    WINDOWPLACEMENT window_placement_before_fullscreen;
    Engine engine;
};

struct Win32_Dynamic_Library
{
    const char *filename;
    const char *temp_filename;
    FILETIME last_write_time;
    HMODULE handle;
};

internal_function void win32_report_last_error_and_exit(char *message)
{
    // note(amer): https://learn.microsoft.com/en-us/windows/win32/debug/retrieving-the-last-error-code
    DWORD error_code = GetLastError();

    LPVOID message_buffer;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|
                  FORMAT_MESSAGE_FROM_SYSTEM|
                  FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&message_buffer, 0, NULL);

    LPVOID display_buffer = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
                                               (lstrlen((LPCTSTR)message_buffer) + lstrlen((LPCTSTR)message) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)display_buffer,
                    LocalSize(display_buffer) / sizeof(TCHAR),
                    TEXT("%s\nerror code %d: %s"),
                    message, error_code, message_buffer);

    MessageBox(NULL, (LPCTSTR)display_buffer, TEXT("Error"), MB_OK);

    LocalFree(message_buffer);
    LocalFree(display_buffer);
    ExitProcess(error_code);
}

internal_function void
win32_set_window_client_size(Win32_State *win32_state, U32 client_width, U32 client_height)
{
    RECT window_rect;
    window_rect.left = 0;
    window_rect.right = client_width;
    window_rect.top = 0;
    window_rect.bottom = client_height;

    Assert(AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE));
    win32_state->window_width = window_rect.right - window_rect.left;
    win32_state->window_height = window_rect.bottom - window_rect.top;
    win32_state->window_client_width = client_width;
    win32_state->window_client_height = client_height;
}

void platform_toggle_fullscreen(Engine *engine)
{
    Win32_State *win32_state = (Win32_State *)engine->platform_state;

    DWORD style = GetWindowLong(win32_state->window, GWL_STYLE);
    if ((style & WS_OVERLAPPEDWINDOW))
    {
        MONITORINFO monitor_info = { sizeof(MONITORINFO) };
        HMONITOR monitor = MonitorFromWindow(win32_state->window, MONITOR_DEFAULTTOPRIMARY);

        if (GetWindowPlacement(win32_state->window,
                               &win32_state->window_placement_before_fullscreen) &&
            GetMonitorInfo(monitor, &monitor_info))
        {
            SetWindowLong(win32_state->window, GWL_STYLE, style & ~(WS_OVERLAPPEDWINDOW));
            SetWindowPos(win32_state->window, HWND_TOP,
                         monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.top,
                         monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            win32_state->engine.window_mode = WindowMode_Fullscreen;
        }
    }
    else
    {
        SetWindowLong(win32_state->window, GWL_STYLE, style|WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(win32_state->window, &win32_state->window_placement_before_fullscreen);
        SetWindowPos(win32_state->window,
                     NULL, 0, 0, 0, 0,
                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);

        win32_state->engine.window_mode = WindowMode_Windowed;
    }
}


void* platform_create_vulkan_surface(Engine *engine, void *instance)
{
    Win32_State *win32_state = (Win32_State *)engine->platform_state;

    VkWin32SurfaceCreateInfoKHR surface_create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surface_create_info.hwnd = win32_state->window;
    surface_create_info.hinstance = win32_state->instance;

    VkSurfaceKHR surface = 0;
    VkResult result = vkCreateWin32SurfaceKHR((VkInstance)instance, &surface_create_info, nullptr, &surface);
    Assert(result == VK_SUCCESS);
    return surface;
}

LRESULT CALLBACK
win32_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    local_presist Win32_State *win32_state;

    LRESULT result = 0;

    switch (message)
    {
        case WM_CREATE:
        {
            CREATESTRUCTA *create_struct = (CREATESTRUCTA *)l_param;
            win32_state = (Win32_State *)create_struct->lpCreateParams;
        } break;

        case WM_CLOSE:
        {
            Event event = {};
            event.type = EventType_Close;
            win32_state->engine.game_code.on_event(&win32_state->engine, event);
            win32_state->engine.is_running = false;
        } break;

        case WM_SETCURSOR:
        {
            bool is_cursor_hovering_over_window_client_area = LOWORD(l_param) == HTCLIENT;
            if (is_cursor_hovering_over_window_client_area)
            {
                if (win32_state->engine.show_cursor)
                {
                    SetCursor(win32_state->cursor);
                }
                else
                {
                    SetCursor(NULL);
                }
                result = TRUE;
            }
            else
            {
                result = DefWindowProc(window, message, w_param, l_param);
            }
        } break;

        case WM_SIZE:
        {
            Event event = {};
            event.type = EventType_Resize;

            if (w_param == SIZE_MAXIMIZED)
            {
                win32_state->engine.is_minimized = false;
                event.maximized = true;
            }
            else if (w_param == SIZE_MINIMIZED)
            {
                win32_state->engine.is_minimized = true;
                event.minimized = true;
            }
            else if (w_param == SIZE_RESTORED)
            {
                win32_state->engine.is_minimized = false;
                event.restored = true;
            }

            U32 client_width = u64_to_u32(l_param & 0xFFFF);
            U32 client_height = u64_to_u32(l_param >> 16);
            win32_set_window_client_size(win32_state, client_width, client_height);

            win32_state->engine.renderer_state.back_buffer_width = client_width;
            win32_state->engine.renderer_state.back_buffer_height = client_height;

            // todo(amer): resizing camera here for now...
            win32_state->engine.renderer_state.camera.aspect_ratio = (F32)client_width / (F32)client_height;
            update_camera(&win32_state->engine.renderer_state.camera);

            if (win32_state->engine.renderer.on_resize)
            {
                win32_state->engine.renderer.on_resize(&win32_state->engine.renderer_state,
                                                       client_width,
                                                       client_height);
            }
            event.width = u32_to_u16(client_width);
            event.height = u32_to_u16(client_height);
            win32_state->engine.game_code.on_event(&win32_state->engine, event);
        } break;

        default:
        {
            result = DefWindowProc(window, message, w_param, l_param);
        } break;
    }

    return result;
}

internal_function FILETIME
win32_get_file_last_write_time(const char *filename)
{
    FILETIME result = {};

    WIN32_FIND_DATAA find_data = {};
    HANDLE find_handle = FindFirstFileA(filename, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE)
    {
        result = find_data.ftLastWriteTime;
        FindClose(find_handle);
    }
    return result;
}

internal_function bool
win32_load_game_code(Win32_Dynamic_Library *win32_dynamic_library,
                     Game_Code *game_code)
{
    bool result = true;

    game_code->init_game = nullptr;
    game_code->on_event = nullptr;
    game_code->on_update = nullptr;

    // todo(amer): freeing the game_temp.dll take a while and the file is locked
    while (CopyFileA(win32_dynamic_library->filename,
                     win32_dynamic_library->temp_filename, FALSE) == 0)
    {
    }

    DWORD flags = DONT_RESOLVE_DLL_REFERENCES|LOAD_IGNORE_CODE_AUTHZ_LEVEL;
    win32_dynamic_library->handle = LoadLibraryExA(win32_dynamic_library->temp_filename, NULL, flags);

    if (win32_dynamic_library->handle)
    {
        Init_Game_Proc init_game_proc =
            (Init_Game_Proc)GetProcAddress(win32_dynamic_library->handle, "init_game");

        if (init_game_proc)
        {
            game_code->init_game = init_game_proc;
        }

        On_Event_Proc on_event_proc =
            (On_Event_Proc)GetProcAddress(win32_dynamic_library->handle, "on_event");

        if (on_event_proc)
        {
            game_code->on_event = on_event_proc;
        }

        On_Update_Proc on_update_proc =
            (On_Update_Proc)GetProcAddress(win32_dynamic_library->handle, "on_update");

        if (on_update_proc)
        {
            game_code->on_update = on_update_proc;
        }

        result = init_game_proc && on_event_proc && on_update_proc;
    }
    else
    {
        result = false;
    }

    return result;
}

internal_function bool
win32_reload_game_code(Win32_Dynamic_Library *win32_dynamic_library, Game_Code *game_code)
{
    bool result = true;

    if (win32_dynamic_library->handle != NULL)
    {
        if (FreeLibrary(win32_dynamic_library->handle) == 0)
        {
            result = false;
        }
        win32_dynamic_library->handle = NULL;
    }

    bool loaded = win32_load_game_code(win32_dynamic_library, game_code);
    if (!loaded)
    {
        result = false;
    }
    return result;
}

// todo(amer): force inline
internal_function inline void
win32_handle_mouse_input(Event *event, MSG message)
{
    event->type = EventType_Mouse;

    if (message.message == WM_LBUTTONDOWN || message.message == WM_LBUTTONUP)
    {
        event->button = VK_LBUTTON;
    }

    if (message.message == WM_MBUTTONDOWN || message.message == WM_MBUTTONUP)
    {
        event->button = VK_MBUTTON;
    }

    if (message.message == WM_RBUTTONDOWN || message.message == WM_RBUTTONUP)
    {
        event->button = VK_RBUTTON;
    }

    if (message.wParam & MK_XBUTTON1)
    {
        event->button = VK_XBUTTON1;
    }

    if (message.wParam & MK_XBUTTON2)
    {
        event->button = VK_XBUTTON2;
    }

    if (message.wParam & MK_SHIFT)
    {
        event->is_shift_down = true;
    }

    if (message.wParam & MK_CONTROL)
    {
        event->is_control_down = true;
    }

    U16 mouse_x = (U16)(message.lParam & 0xFF);
    U16 mouse_y = (U16)(message.lParam >> 16);
    event->mouse_x = mouse_x;
    event->mouse_y = mouse_y;
}

INT WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous_instance, PSTR command_line, INT show)
{
    (void)previous_instance;
    (void)command_line;
    (void)show;

    HANDLE mutex = CreateMutexA(NULL, FALSE, HE_APP_NAME "_Mutex");

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxA(NULL, "application is already running", "Error", MB_OK);
        return 0;
    }
    else if (mutex == NULL)
    {
        win32_report_last_error_and_exit("failed to create mutex: " HE_APP_NAME "_Mutex");
    }

    // todo(amer): engine configuration should be outside win32_main
    Engine_Configuration configuration = {};
    configuration.permanent_memory_size = HE_MegaBytes(64);
    configuration.transient_memory_size = HE_MegaBytes(256);
    configuration.show_cursor = true;
    configuration.lock_cursor = false;
    configuration.window_mode = WindowMode_Windowed;
    configuration.back_buffer_width = 1280;
    configuration.back_buffer_height = 720;

    Win32_State *win32_state = (Win32_State *)VirtualAlloc(0,
                                                           sizeof(win32_state),
                                                           MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    win32_state->instance = instance;
    win32_state->cursor = LoadCursor(NULL, IDC_ARROW);

    Win32_Dynamic_Library win32_dynamic_library = {};
    win32_dynamic_library.filename = "../bin/game.dll";
    win32_dynamic_library.temp_filename = "../bin/game_temp.dll";
    win32_dynamic_library.last_write_time = win32_get_file_last_write_time(win32_dynamic_library.filename);

    win32_load_game_code(&win32_dynamic_library, &win32_state->engine.game_code);
    win32_set_window_client_size(win32_state,
                                 configuration.back_buffer_width,
                                 configuration.back_buffer_height);

    WNDCLASSA window_class = {};
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = HE_WINDOW_CLASS_NAME;
    window_class.hCursor = win32_state->cursor;
    // todo(amer): in the future we should be load icons from disk
    window_class.hIcon = NULL;

    if (RegisterClassA(&window_class) == 0)
    {
        win32_report_last_error_and_exit("failed to register window class");
    }

    win32_state->window = CreateWindowExA(0, HE_WINDOW_CLASS_NAME, HE_APP_NAME,
                                         WS_OVERLAPPEDWINDOW,
                                         CW_USEDEFAULT, CW_USEDEFAULT,
                                         win32_state->window_width, win32_state->window_height,
                                         NULL, NULL, instance, win32_state);
    if (win32_state->window == NULL)
    {
        win32_report_last_error_and_exit("failed to create a window");
    }

    ShowWindow(win32_state->window, SW_SHOW);

    Engine *engine = &win32_state->engine;
    Game_Code *game_code = &engine->game_code;

    bool started = startup(engine, configuration, win32_state);
    engine->is_running = started;

    LARGE_INTEGER performance_frequency;
    Assert(QueryPerformanceFrequency(&performance_frequency));
    S64 counts_per_second = performance_frequency.QuadPart;

    LARGE_INTEGER last_counter;
    Assert(QueryPerformanceCounter(&last_counter));

    while (engine->is_running)
    {
        LARGE_INTEGER current_counter;
        Assert(QueryPerformanceCounter(&current_counter));

        S64 elapsed_counts = current_counter.QuadPart - last_counter.QuadPart;
        F64 elapsed_time = (F64)elapsed_counts / (F64)counts_per_second;
        last_counter = current_counter;
        F32 delta_time = (F32)elapsed_time;

        FILETIME last_write_time =
            win32_get_file_last_write_time(win32_dynamic_library.filename);

        if (CompareFileTime(&last_write_time, &win32_dynamic_library.last_write_time) != 0)
        {
            if (win32_reload_game_code(&win32_dynamic_library, &engine->game_code))
            {
                win32_dynamic_library.last_write_time = last_write_time;
            }
            else
            {
                set_game_code_to_stubs(&engine->game_code);
            }
        }

        MSG message = {};
        while (PeekMessageA(&message, win32_state->window, 0, 0, PM_REMOVE))
        {
            switch (message.message)
            {
                case WM_SYSKEYDOWN:
                case WM_KEYDOWN:
                case WM_SYSKEYUP:
                case WM_KEYUP:
                {
                    U16 key_code = (U16)message.wParam;
                    bool was_down = (message.lParam & (1u << 30));
                    bool is_down = (message.lParam & (1u << 31)) == 0;

                    if (key_code == VK_SHIFT)
                    {
                        if (GetKeyState(VK_LSHIFT) & 0x8000)
                        {
                            key_code = VK_LSHIFT;
                        }
                        else if (GetKeyState(VK_RSHIFT) & 0x8000)
                        {
                            key_code = VK_RSHIFT;
                        }
                    }

                    if (key_code == VK_MENU)
                    {
                        if (GetKeyState(VK_LMENU) & 0x8000)
                        {
                            key_code = VK_LMENU;
                        }
                        else if (GetKeyState(VK_RMENU) & 0x8000)
                        {
                            key_code = VK_RMENU;
                        }
                    }

                    Event event = {};
                    event.type = EventType_Key;
                    event.key = key_code;

                    if (is_down)
                    {
                        if (was_down)
                        {
                            event.held = true;
                            engine->input.key_states[key_code] = InputState_Held;
                        }
                        else
                        {
                            event.pressed = true;
                            engine->input.key_states[key_code] = InputState_Pressed;
                        }
                    }
                    else
                    {
                        engine->input.key_states[key_code] = InputState_Released;
                    }

                    game_code->on_event(engine, event);
                } break;

                case WM_LBUTTONDOWN:
                case WM_MBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_XBUTTONDOWN:
                {
                    Event event = {};
                    win32_handle_mouse_input(&event, message);
                    event.pressed = true;
                    event.held = true;

                    Input *input = &engine->input;
                    input->button_states[event.button] = InputState_Pressed;

                    game_code->on_event(engine, event);
                } break;

                case WM_LBUTTONUP:
                case WM_MBUTTONUP:
                case WM_RBUTTONUP:
                case WM_XBUTTONUP:
                {
                    Event event = {};
                    win32_handle_mouse_input(&event, message);

                    Input *input = &engine->input;
                    input->button_states[event.button] = InputState_Released;

                    game_code->on_event(engine, event);
                } break;

                case WM_LBUTTONDBLCLK:
                case WM_MBUTTONDBLCLK:
                case WM_RBUTTONDBLCLK:
                case WM_XBUTTONDBLCLK:
                {
                    Event event = {};
                    win32_handle_mouse_input(&event, message);
                    event.double_click = true;
                    game_code->on_event(engine, event);
                } break;

                case WM_NCMOUSEMOVE:
                case WM_MOUSEMOVE:
                {
                    Event event = {};
                    win32_handle_mouse_input(&event, message);
                    game_code->on_event(engine, event);
                } break;

                case WM_MOUSEWHEEL:
                {
                    S32 delta = u64_to_s32(message.wParam >> 16);
                    win32_state->mouse_wheel_accumulated_delta += delta;

                    Event event = {};
                    event.type = EventType_Mouse;

                    while (win32_state->mouse_wheel_accumulated_delta >= 120)
                    {
                        event.mouse_wheel_up = true;
                        game_code->on_event(engine, event);
                        win32_state->mouse_wheel_accumulated_delta -= 120;
                    }

                    while (win32_state->mouse_wheel_accumulated_delta <= -120)
                    {
                        event.mouse_wheel_down = true;
                        game_code->on_event(engine, event);
                        win32_state->mouse_wheel_accumulated_delta += 120;
                    }
                } break;

                default:
                {
                    // TranslateMessage(&message);
                    DispatchMessageA(&message);
                } break;
            }
        }

        Input *input = &engine->input;

        RECT window_rect;
        GetWindowRect(win32_state->window, &window_rect);

        POINT cursor;
        GetCursorPos(&cursor);

        input->mouse_x = (U16)cursor.x;
        input->mouse_y = (U16)cursor.y;
        input->mouse_delta_x = (S32)input->mouse_x - (S32)input->prev_mouse_x;
        input->mouse_delta_y = (S32)input->mouse_y - (S32)input->prev_mouse_y;
        

        if (engine->lock_cursor)
        {
            input->prev_mouse_x = u32_to_u16((window_rect.left + window_rect.right) / 2);
            input->prev_mouse_y = u32_to_u16((window_rect.top + window_rect.bottom) / 2);
            SetCursorPos((window_rect.left + window_rect.right) / 2, (window_rect.top + window_rect.bottom) / 2);
            ClipCursor(&window_rect);
        }
        else
        {
            input->prev_mouse_x = input->mouse_x;
            input->prev_mouse_y = input->mouse_y;
            ClipCursor(NULL);
        }

        game_loop(engine, delta_time);
    }

    shutdown(engine);

    return 0;
}

void* platform_allocate_memory(U64 size)
{
    Assert(size);
    return VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
}

void platform_deallocate_memory(void *memory)
{
    Assert(memory);
    VirtualFree(memory, 0, MEM_RELEASE);
}

Platform_File_Handle platform_open_file(const char *filename, File_Operation operations)
{
    Platform_File_Handle result = {};

    DWORD access_flags = 0;

    if ((operations & FileOperation_Read) && (operations & FileOperation_Write))
    {
        access_flags = GENERIC_READ|GENERIC_WRITE;
    }
    else if ((operations & FileOperation_Read))
    {
        access_flags = GENERIC_READ;
    }
    else if ((operations & FileOperation_Write))
    {
        access_flags = GENERIC_WRITE;
    }

    result.platform_data = CreateFileA(filename, access_flags, FILE_SHARE_READ,
                                       0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    return result;
}

bool platform_is_file_handle_valid(Platform_File_Handle file_handle)
{
    bool result = file_handle.platform_data != INVALID_HANDLE_VALUE;
    return result;
}

U64 platform_get_file_size(Platform_File_Handle file_handle)
{
    LARGE_INTEGER result = {};
    BOOL return_value = GetFileSizeEx(file_handle.platform_data, &result);
    Assert(return_value);
    return result.QuadPart;
}

bool platform_read_data_from_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a read of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(file_handle.platform_data, data,
                           u64_to_u32(size), &read_bytes, &overlapped);
    return result == TRUE && read_bytes == size;
}

bool platform_write_data_to_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a write of 4GBs
    DWORD written_bytes;
    BOOL result = WriteFile(file_handle.platform_data, data,
                            u64_to_u32(size), &written_bytes, &overlapped);
    return result == TRUE && written_bytes == size;
}

bool platform_close_file(Platform_File_Handle file_handle)
{
    bool result = CloseHandle(file_handle.platform_data) != 0;
    return result;
}

Read_Entire_File_Result platform_begin_read_entire_file(const char *filename)
{
    Read_Entire_File_Result result = {};
    HANDLE win32_file_handle = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ,
                                           0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (win32_file_handle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER size_result = {};
        if (GetFileSizeEx(win32_file_handle, &size_result))
        {
            U64 size = size_result.QuadPart;
            if (size)
            {
                result.file_handle.platform_data = win32_file_handle;
                result.size = size;
                result.success = true;
            }
            else
            {
                CloseHandle(win32_file_handle);
            }
        }
    }

    return result;
}

bool platform_end_read_entire_file(Read_Entire_File_Result *read_entire_file_result,
                              void *data)
{
    // note(amer): we are only limited to a write of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(read_entire_file_result->file_handle.platform_data, data,
                           u64_to_u32(read_entire_file_result->size), &read_bytes, NULL);
    CloseHandle(read_entire_file_result->file_handle.platform_data);
    return result == TRUE && read_bytes == read_entire_file_result->size;
}

void platform_report_error_and_exit(const char *message, ...)
{
    local_presist char string_buffer[1024];
    va_list arg_list;

    va_start(arg_list, message);
    S32 written = vsprintf(string_buffer, message, arg_list);
    Assert(written >= 0);

    MessageBoxA(NULL, string_buffer, "Error", MB_OK);

    va_end(arg_list);
}

void platform_debug_printf(const char *message, ...)
{
    local_presist char string_buffer[1024];
    va_list arg_list;

    va_start(arg_list, message);
    S32 written = vsprintf(string_buffer, message, arg_list);
    Assert(written >= 0);
    OutputDebugStringA(string_buffer);
    va_end(arg_list);
}