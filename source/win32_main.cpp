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
win32_toggle_fullscreen(Win32_State *win32_state)
{
    DWORD style = GetWindowLong(win32_state->window, GWL_STYLE);
    if (style & WS_OVERLAPPEDWINDOW)
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
            if (w_param == SIZE_MAXIMIZED)
            {
            }
            else if (w_param == SIZE_MINIMIZED)
            {
            }
            else if (w_param == SIZE_RESTORED)
            {
            }

            U32 client_width = u64_to_u32(l_param & 0xFFFF);
            U32 client_height = u64_to_u32(l_param >> 16);
            win32_set_window_client_size(win32_state, client_width, client_height);

            Event event = {};
            event.type = EventType_Resize;
            event.width = client_width;
            event.height = client_height;
            win32_state->engine.game_code.on_event(&win32_state->engine, event);
        } break;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        {
            Event event = {};
            event.type = EventType_Mouse;
            event.pressed = false;

            U64 mouse_x = l_param & 0xFF;
            U64 mouse_y = l_param >> 16;
            event.mouse_x = (U16)mouse_x;
            event.mouse_y = (U16)mouse_y;

            // U64 xbutton = w_param >> 16;
            // if (xbutton == XBUTTON1)
            // {
            // }
            // else if (xbutton == XBUTTON2)
            // {
            // }

            if (w_param & MK_CONTROL)
            {
            }

            if (w_param & MK_LBUTTON)
            {
                event.button = VK_LBUTTON;
                event.pressed = true;
            }

            if (w_param & MK_MBUTTON)
            {
                event.button = VK_MBUTTON;
                event.pressed = true;
            }

            if (w_param & MK_RBUTTON)
            {
                event.button = VK_RBUTTON;
                event.pressed = true;
            }

            if (w_param & MK_SHIFT)
            {
            }

            if (w_param & MK_XBUTTON1)
            {
                event.button = VK_XBUTTON1;
                event.pressed = true;
            }

            if (w_param & MK_XBUTTON2)
            {
                event.button = VK_XBUTTON2;
                event.pressed = true;
            }

            win32_state->engine.game_code.on_event(&win32_state->engine, event);
        } break;

        case WM_MOUSEWHEEL:
        {
            S32 delta = u64_to_s32(w_param >> 16);
            win32_state->mouse_wheel_accumulated_delta += delta;

            Event event = {};
            event.type = EventType_Mouse;

            while (win32_state->mouse_wheel_accumulated_delta >= 120)
            {
                event.mouse_wheel_up = true;
                win32_state->engine.game_code.on_event(&win32_state->engine, event);

                win32_state->mouse_wheel_accumulated_delta -= 120;
            }

            while (win32_state->mouse_wheel_accumulated_delta <= -120)
            {
                event.mouse_wheel_down = true;
                win32_state->engine.game_code.on_event(&win32_state->engine, event);

                win32_state->mouse_wheel_accumulated_delta += 120;
            }
        } break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            U64 virtual_key_code = w_param;
            bool was_down = l_param & (1 << 30);
            bool is_down = (l_param & (1 << 31)) == 0;
            bool is_held = is_down && was_down;

            Event event = {};
            event.type = EventType_Key;
            event.key = (U16)virtual_key_code;
            event.pressed = is_down;
            event.held = is_held;
            win32_state->engine.game_code.on_event(&win32_state->engine, event);

        } break;

        default:
        {
            result = DefWindowProc(window, message, w_param, l_param);
        } break;
    }

    return result;
}

internal_function bool
win32_load_game_code(Game_Code *game_code)
{
    HMODULE handle = LoadLibraryA("../bin/game.dll");
    if (handle)
    {
        game_code->init_game = (Init_Game_Proc)GetProcAddress(handle, "init_game");
        game_code->on_event  = (On_Event_Proc)GetProcAddress(handle, "on_event");
        game_code->on_update = (On_Update_Proc)GetProcAddress(handle, "on_update");
    }
    return false;
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
    Engine_Configuration configuration;
    configuration.permanent_memory_size = HE_MegaBytes(16);
    configuration.transient_memory_size = HE_MegaBytes(32);
    configuration.show_cursor = true;
    configuration.window_mode = WindowMode_Windowed;

    // todo(amer): this should be allocated on the heap
    Win32_State win32_state = {};
    win32_state.instance = instance;
    win32_state.cursor = LoadCursor(NULL, IDC_ARROW);

    win32_load_game_code(&win32_state.engine.game_code);
    win32_set_window_client_size(&win32_state, 1280, 720);

    WNDCLASSA window_class = {};
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = HE_WINDOW_CLASS_NAME;
    window_class.hCursor = win32_state.cursor;

    // todo(amer): in the future we should be load icons from disk
    window_class.hIcon = NULL;

    if (RegisterClassA(&window_class) == 0)
    {
        win32_report_last_error_and_exit("failed to register window class");
    }

    win32_state.window = CreateWindowExA(0, HE_WINDOW_CLASS_NAME, HE_APP_NAME,
                                         WS_OVERLAPPEDWINDOW,
                                         CW_USEDEFAULT, CW_USEDEFAULT,
                                         win32_state.window_width, win32_state.window_height,
                                         NULL, NULL, instance, &win32_state);
    if (win32_state.window == NULL)
    {
        win32_report_last_error_and_exit("failed to create a window");
    }

    ShowWindow(win32_state.window, SW_SHOW);

    if (configuration.window_mode == WindowMode_Fullscreen)
    {
        win32_toggle_fullscreen(&win32_state);
    }

    LARGE_INTEGER performance_frequency;
    HE_Assert(QueryPerformanceFrequency(&performance_frequency));
    S64 counts_per_second = performance_frequency.QuadPart;

    bool started = startup(&win32_state.engine, configuration);
    win32_state.engine.is_running = started;

    LARGE_INTEGER last_counter;
    HE_Assert(QueryPerformanceCounter(&last_counter));

    while (win32_state.engine.is_running)
    {
        LARGE_INTEGER current_counter;
        HE_Assert(QueryPerformanceCounter(&current_counter));

        S64 elapsed_counts = current_counter.QuadPart - last_counter.QuadPart;
        F64 elapsed_time = (F64)elapsed_counts / (F64)counts_per_second;
        last_counter = current_counter;

        F32 delta_time = (F32)elapsed_time;

        MSG message = {};
        while (PeekMessageA(&message, win32_state.window, 0, 0, PM_REMOVE))
        {
            // TranslateMessage(&message);
            // todo(amer): we want to handle events here and dispatch messages we don't want
            DispatchMessageA(&message);
        }

        game_loop(&win32_state.engine, delta_time);

        // SHORT key_state = GetAsyncKeyState(VK_ESCAPE);
        // bool is_down = (key_state & (1 << 15));
        // bool was_down = (key_state & 1);
		// bool is_held = was_down && is_down;
		// if (is_down || was_down)
        // {
        //     win32_state.is_running = false;
        // }
    }

    shutdown(&win32_state.engine);

    return 0;
}