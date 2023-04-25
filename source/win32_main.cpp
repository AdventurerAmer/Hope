#include "win32_platform.cpp"

#if HE_UNITY_BUILD
#include "translation_units.h"
#endif

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

void
win32_report_last_error_and_exit(char *message)
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
win32_set_window_client_size(Win32_State *win32_state,
                             U32 client_width, U32 client_height)
{
    RECT window_rect;
    window_rect.left = 0;
    window_rect.right = client_width;
    window_rect.top = 0;
    window_rect.bottom = client_height;

    HE_Assert(AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE));
    win32_state->window_width = window_rect.right - window_rect.left;
    win32_state->window_height = window_rect.bottom - window_rect.top;
    win32_state->window_client_width = client_width;
    win32_state->window_client_height = client_height;
}

internal_function void
platform_toggle_fullscreen(Engine *engine)
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

        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC hdc = BeginPaint(win32_state->window, &paint);
            FillRect(hdc, &paint.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
            EndPaint(win32_state->window, &paint);
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
                event.maximized = true;
            }
            else if (w_param == SIZE_MINIMIZED)
            {
                event.minimized = true;
            }
            else if (w_param == SIZE_RESTORED)
            {
                event.restored = true;
            }

            U32 client_width = u64_to_u32(l_param & 0xFFFF);
            U32 client_height = u64_to_u32(l_param >> 16);
            win32_set_window_client_size(win32_state, client_width, client_height);

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

    WIN32_FIND_DATA find_data = {};
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
    configuration.permanent_memory_size = HE_MegaBytes(16);
    configuration.transient_memory_size = HE_MegaBytes(32);
    configuration.show_cursor = true;
    configuration.window_mode = WindowMode_Windowed;
    configuration.back_buffer_width = 1280;
    configuration.back_buffer_height = 720;

    Win32_State *win32_state = (Win32_State *)VirtualAlloc(0,
                                                           sizeof(win32_state),
                                                           MEM_COMMIT, PAGE_READWRITE);
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

    // note(amer): temprary for testing...
    Vulkan_Context context = {};
    init_vulkan(&context, win32_state->window,
                win32_state->instance,
                &engine->memory.permanent_arena);

    LARGE_INTEGER performance_frequency;
    HE_Assert(QueryPerformanceFrequency(&performance_frequency));
    S64 counts_per_second = performance_frequency.QuadPart;

    LARGE_INTEGER last_counter;
    HE_Assert(QueryPerformanceCounter(&last_counter));

    while (engine->is_running)
    {
        LARGE_INTEGER current_counter;
        HE_Assert(QueryPerformanceCounter(&current_counter));

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
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                case WM_XBUTTONDOWN:
                case WM_XBUTTONUP:
                case WM_MOUSEMOVE:
                case WM_LBUTTONDBLCLK:
                case WM_MBUTTONDBLCLK:
                case WM_RBUTTONDBLCLK:
                case WM_XBUTTONDBLCLK:
                {
                    Event event = {};
                    event.type = EventType_Mouse;

                    if (message.message == WM_LBUTTONDBLCLK ||
                        message.message == WM_MBUTTONDBLCLK ||
                        message.message == WM_RBUTTONDBLCLK ||
                        message.message == WM_XBUTTONDBLCLK)
                    {
                        event.double_click = true;
                    }

                    U64 mouse_x = message.lParam & 0xFF;
                    U64 mouse_y = message.lParam >> 16;
                    event.mouse_x = (U16)mouse_x;
                    event.mouse_y = (U16)mouse_y;

                    if (message.wParam & MK_CONTROL)
                    {
                        event.is_control_down = true;
                    }

                    if (message.wParam & MK_LBUTTON)
                    {
                        event.button = VK_LBUTTON;
                        event.pressed = true;
                    }

                    if (message.wParam & MK_MBUTTON)
                    {
                        event.button = VK_MBUTTON;
                        event.pressed = true;
                    }

                    if (message.wParam & MK_RBUTTON)
                    {
                        event.button = VK_RBUTTON;
                        event.pressed = true;
                    }

                    if (message.wParam & MK_SHIFT)
                    {
                        event.is_shift_down = true;
                    }

                    if (message.wParam & MK_XBUTTON1)
                    {
                        event.button = VK_XBUTTON1;
                        event.pressed = true;
                    }

                    if (message.wParam & MK_XBUTTON2)
                    {
                        event.button = VK_XBUTTON2;
                        event.pressed = true;
                    }

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

                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                case WM_KEYDOWN:
                case WM_KEYUP:
                {
                    U64 virtual_key_code = message.wParam;
                    bool was_down = message.lParam & (1 << 30);
                    bool is_down = (message.lParam & (1 << 31)) == 0;
                    bool is_held = is_down && was_down;
                    Event event = {};
                    event.type = EventType_Key;
                    event.key = (U16)virtual_key_code;
                    event.pressed = is_down;
                    event.held = is_held;
                    game_code->on_event(engine, event);

                } break;

                default:
                {
                    // TranslateMessage(&message);
                    DispatchMessageA(&message);
                } break;
            }
        }

        game_loop(engine, delta_time);
        vulkan_draw(&context);
    }

    // todo(amer): temprary for testing...
    deinit_vulkan(&context);
    shutdown(engine);

    return 0;
}