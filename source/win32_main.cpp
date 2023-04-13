#include "win32_platform.h"

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
    bool is_running;
};

internal_function LRESULT CALLBACK
win32_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

internal_function void
win32_report_last_error_and_exit(char *message);

internal_function void
win32_set_window_client_size(Win32_State *win32_state,
                             U32 client_width, U32 client_height);

INT WINAPI
WinMain(HINSTANCE instance, HINSTANCE previous_instance, PSTR command_line, INT show)
{
    (void)previous_instance;
    (void)command_line;
    (void)show;

    Win32_State win32_state = {};
    win32_state.instance = instance;
    win32_set_window_client_size(&win32_state, 1280, 720);
	
    HANDLE mutex = CreateMutexA(NULL, FALSE, HE_APP_NAME "_Mutex");
	
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxA(NULL, "application is already running", "Error", MB_OK);
        return 0;
    }
    else if (mutex == NULL)
    {
        win32_report_last_error_and_exit("failed to create mutex: " HE_APP_NAME "_Mutex");
        return 0;
    }
	
    WNDCLASSA window_class = {};
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = HE_WINDOW_CLASS_NAME;

    // todo(amer): in the future we should be load icons and cursors from disk
    window_class.hIcon = NULL;
	window_class.hCursor = NULL;
	
    if (RegisterClassA(&window_class) == 0)
    {
        win32_report_last_error_and_exit("failed to register window class");
        return 0;
    }

    win32_state.window = CreateWindowExA(0, HE_WINDOW_CLASS_NAME, HE_APP_NAME,
                                         WS_OVERLAPPEDWINDOW,
                                         CW_USEDEFAULT, CW_USEDEFAULT,
                                         win32_state.window_width, win32_state.window_height,
                                         NULL, NULL, instance, &win32_state);
    if (win32_state.window == NULL)
    {
        win32_report_last_error_and_exit("failed to create a window");
        return 0;
    }
	
    ShowWindow(win32_state.window, SW_SHOW);

    // note(amer): maybe we want to heap allocate engine in the future.
    Engine engine = {};

    // todo(amer): engine configuration should be outside win32_main
    Engine_Configuration configuration;
    configuration.permanent_memory_size = HE_MegaBytes(32);
    configuration.transient_memory_size = HE_MegaBytes(64);
    bool started = startup(&engine, configuration);

    U64 *foo = ArenaPush(&engine.permanent_arena, U64);

    Temprary_Memory_Arena temp_arena = begin_temprary_memory_arena(&engine.permanent_arena);
    U64 *bar = ArenaPush(&temp_arena, U64);
    end_temprary_memory_arena(&temp_arena);

    {
        Scoped_Temprary_Memory_Arena scoped_temp_arena(&engine.permanent_arena);
        U64 *car = ArenaPush(&scoped_temp_arena, U64);
    }

    win32_state.is_running = started;
    while (win32_state.is_running)
    {
        MSG message = {};
        while (PeekMessageA(&message, win32_state.window, 0, 0, PM_REMOVE))
        {
            // TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        SHORT key_state = GetAsyncKeyState(VK_ESCAPE);
        bool is_down = (key_state & (1 << 15));
        bool was_down = (key_state & 1);
        if (is_down || was_down)
        {
            win32_state.is_running = false;
        }
    }

    shutdown(&engine);
    return 0;
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
            HE_DebugPrintf(Core, Trace, "Window Created\n");

            CREATESTRUCTA *create_struct = (CREATESTRUCTA *)l_param;
            win32_state = (Win32_State *)create_struct->lpCreateParams;
        } break;
		
        // the user clicked the window exit button or alt+f4
        case WM_CLOSE:
        {
            HE_DebugPrintf(Core, Trace, "Window Closed\n");
            win32_state->is_running = false;
        } break;
		
        case WM_SIZE:
        {
            if (w_param == SIZE_MAXIMIZED)
            {
                HE_DebugPrintf(Core, Trace, "Maximized\n");
            }
            else if (w_param == SIZE_MINIMIZED)
            {
                HE_DebugPrintf(Core, Trace, "Minimized\n");
            }
            else if (w_param == SIZE_RESTORED)
            {
                HE_DebugPrintf(Core, Trace, "Restored\n");
            }
			
            U32 client_width = u64_to_u32(l_param & 0xFFFF);
            U32 client_height = u64_to_u32(l_param >> 16);
            win32_set_window_client_size(win32_state, client_width, client_height);

            HE_DebugPrintf(Core, Trace, "Window Resize: (%u, %u)\n", client_width, client_height);
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
            U64 mouse_x = l_param & 0xFF;
            U64 mouse_y = l_param >> 16;
            HE_DebugPrintf(Core, Trace, "Mouse: (%llu, %llu)\n", mouse_x, mouse_y);

            U64 xbutton = w_param >> 16;

            if (xbutton == XBUTTON1)
            {
                HE_DebugPrintf(Core, Trace, "XButton1\n");
            }
            else if (xbutton == XBUTTON2)
            {
                HE_DebugPrintf(Core, Trace, "XButton2\n");
            }
			
            if (w_param & MK_CONTROL)
            {
                HE_DebugPrintf(Core, Trace, "Control\n");
            }

            if (w_param & MK_LBUTTON)
            {
                HE_DebugPrintf(Core, Trace, "Left Mouse\n");
            }

            if (w_param & MK_MBUTTON)
            {
                HE_DebugPrintf(Core, Trace, "Middle Button\n");
            }

            if (w_param & MK_RBUTTON)
            {
                HE_DebugPrintf(Core, Trace, "Right Down\n");
            }

            if (w_param & MK_SHIFT)
            {
                HE_DebugPrintf(Core, Trace, "Shift\n");
            }

            if (w_param & MK_XBUTTON1)
            {
                HE_DebugPrintf(Core, Trace, "XButton1\n");
            }

            if (w_param & MK_XBUTTON2)
            {
                HE_DebugPrintf(Core, Trace, "XButton2\n");
            }
        } break;
		
        case WM_MOUSEWHEEL:
        {
            S32 delta = u64_to_s32(w_param >> 16);
            win32_state->mouse_wheel_accumulated_delta += delta;
			
            while (win32_state->mouse_wheel_accumulated_delta >= 120)
            {
                HE_DebugPrintf(Core, Trace, "ScrollUp\n");
                win32_state->mouse_wheel_accumulated_delta -= 120;
            }
			
            while (win32_state->mouse_wheel_accumulated_delta <= -120)
            {
                HE_DebugPrintf(Core, Trace, "ScrollDown\n");
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
            HE_DebugPrintf(Core, Trace,
                           "key_code: %llu, was_down: %d, is_down: %d, is_held: %d\n",
                           virtual_key_code, was_down, is_down, is_held);
        } break;
		
        default:
        {
            result = DefWindowProc(window, message, w_param, l_param);
        } break;
    }
	
    return result;
}

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

internal_function void*
platform_allocate_memory(U64 size)
{
    return VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
}

internal_function void
platform_deallocate_memory(void *memory)
{
    VirtualFree(memory, 0, MEM_RELEASE);
}

internal_function Platform_File_Handle
platform_open_file(const char *filename, File_Operation operations)
{
    Platform_File_Handle result = {};

    DWORD access_flags = 0;

    if ((operations & File_Operation_Read) && (operations & File_Operation_Write))
    {
        access_flags = GENERIC_READ|GENERIC_WRITE;
    }
    else if ((operations & File_Operation_Read))
    {
        access_flags = GENERIC_READ;
    }
    else if ((operations & File_Operation_Write))
    {
        access_flags = GENERIC_WRITE;
    }

    result.win32_file_handle = CreateFileA(filename, access_flags, FILE_SHARE_READ,
                                           0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    return result;
}

internal_function bool
platform_is_file_handle_valid(Platform_File_Handle file_handle)
{
    bool result = file_handle.win32_file_handle != INVALID_HANDLE_VALUE;
    return result;
}

internal_function bool
platform_read_data_from_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a read of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(file_handle.win32_file_handle, data, u64_to_u32(size), &read_bytes, &overlapped);
    return result == TRUE && read_bytes == size;
}

internal_function bool
platform_write_data_to_file(Platform_File_Handle file_handle, U64 offset, void *data, U64 size)
{
    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // note(amer): we are only limited to a write of 4GBs
    DWORD written_bytes;
    BOOL result = WriteFile(file_handle.win32_file_handle, data, u64_to_u32(size), &written_bytes, &overlapped);
    return result == TRUE && written_bytes == size;
}

internal_function bool
platform_close_file(Platform_File_Handle file_handle)
{
    bool result = CloseHandle(file_handle.win32_file_handle) != 0;
    return result;
}

internal_function void
platform_debug_printf(const char *message)
{
    OutputDebugStringA(message);
}