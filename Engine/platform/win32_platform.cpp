#include "core/platform.h"
#include "core/memory.h"
#include "core/engine.h"
#include "core/cvars.h"

#include "core/logging.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "ImGui/backends/imgui_impl_win32.cpp"

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma warning(push, 0)
#include <strsafe.h>
#include <windows.h>
#pragma warning(pop)

struct Win32_Window_State
{
    HWND handle;
    WINDOWPLACEMENT placement_before_fullscreen;
};

struct Win32_Platform_State
{
    HINSTANCE instance;

    const char *window_class_name;

    HCURSOR cursor;
    S32 mouse_wheel_accumulated_delta;

    Engine *engine;
};

static Win32_Platform_State win32_platform_state;

// https://learn.microsoft.com/en-us/windows/win32/Debug/retrieving-the-last-error-code
static void win32_log_last_error()
{
    DWORD error_code = GetLastError();

    LPVOID message_buffer;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&message_buffer, 0, NULL);

    HE_LOG(Core, Fetal, "win32 platform error code %d: %s", error_code, message_buffer);

    LocalFree(message_buffer);
}

static void win32_get_window_size(U32 client_width, U32 client_height, U32 *width, U32 *height)
{
    RECT rect;
    rect.left = 0;
    rect.right = client_width;
    rect.top = 0;
    rect.bottom = client_height;

    BOOL success = AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HE_ASSERT(success != 0);

    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
}

HE_FORCE_INLINE static void win32_handle_mouse_input(Event *event, UINT message, WPARAM w_param, LPARAM l_param)
{
    event->type = Event_Type::MOUSE;

    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
    {
        event->button = VK_LBUTTON;
    }

    if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
    {
        event->button = VK_MBUTTON;
    }

    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
    {
        event->button = VK_RBUTTON;
    }

    if (w_param & MK_XBUTTON1)
    {
        event->button = VK_XBUTTON1;
    }

    if (w_param & MK_XBUTTON2)
    {
        event->button = VK_XBUTTON2;
    }

    if (w_param & MK_SHIFT)
    {
        event->is_shift_down = true;
    }

    if (w_param & MK_CONTROL)
    {
        event->is_control_down = true;
    }

    U16 mouse_x = (U16)(l_param & 0xFF);
    U16 mouse_y = (U16)(l_param >> 16);
    event->mouse_x = mouse_x;
    event->mouse_y = mouse_y;
}

static LRESULT CALLBACK win32_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    static Engine *engine = win32_platform_state.engine;
    LRESULT result = 0;

    switch (message)
    {
        case WM_CLOSE:
        {
            Event event = {};
            event.type = Event_Type::CLOSE;
            on_event(win32_platform_state.engine, event);
            win32_platform_state.engine->is_running = false;
        } break;

        case WM_SETCURSOR:
        {
            bool is_cursor_hovering_over_window_client_area = LOWORD(l_param) == HTCLIENT;
            if (is_cursor_hovering_over_window_client_area)
            {
                if (win32_platform_state.engine->show_cursor)
                {
                    SetCursor(win32_platform_state.cursor);
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
            event.type = Event_Type::RESIZE;

            if (w_param == SIZE_MAXIMIZED)
            {
                win32_platform_state.engine->is_minimized = false;
                event.maximized = true;
            }
            else if (w_param == SIZE_MINIMIZED)
            {
                win32_platform_state.engine->is_minimized = true;
                event.minimized = true;
            }
            else if (w_param == SIZE_RESTORED)
            {
                win32_platform_state.engine->is_minimized = false;
                event.restored = true;
            }

            U32 client_width = u64_to_u32(l_param & 0xFFFF);
            U32 client_height = u64_to_u32(l_param >> 16);

            U32 window_width = 0;
            U32 window_height = 0;
            win32_get_window_size(client_width, client_height, &window_width, &window_height);

            event.client_width = u32_to_u16(client_width);
            event.client_height = u32_to_u16(client_height);
            event.window_width = u32_to_u16(window_width);
            event.window_height = u32_to_u16(window_height);
            on_event(engine, event);
        } break;

        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYUP:
        {
            U16 key_code = (U16)w_param;
            bool was_down = (l_param & (1u << 30));
            bool is_down = (l_param & (1u << 31)) == 0;

            bool is_left_shift_down = GetKeyState(VK_LSHIFT) & 0x8000;
            bool is_right_shift_down = GetKeyState(VK_RSHIFT) & 0x8000;
            bool is_left_control_down = GetKeyState(VK_LCONTROL) & 0x8000;
            bool is_right_control_down = GetKeyState(VK_RCONTROL) & 0x8000;
            bool is_left_alt_down = GetKeyState(VK_LMENU) & 0x8000;
            bool is_right_alt_down = GetKeyState(VK_RMENU) & 0x8000;

            if (key_code == VK_SHIFT)
            {
                if (is_left_shift_down)
                {
                    key_code = VK_LSHIFT;
                }
                else if (is_right_shift_down)
                {
                    key_code = VK_RSHIFT;
                }
            }

            if (key_code == VK_CONTROL)
            {
                if (is_left_control_down)
                {
                    key_code = VK_LCONTROL;
                }
                else if (is_right_control_down)
                {
                    key_code = VK_RCONTROL;
                }
            }

            if (key_code == VK_MENU)
            {
                if (is_left_alt_down)
                {
                    key_code = VK_LMENU;
                }
                else if (is_right_alt_down)
                {
                    key_code = VK_RMENU;
                }
            }

            Input_State input_state = Input_State::RELEASED;

            if (is_down)
            {
                if (was_down)
                {
                    input_state = Input_State::HELD;
                }
                else
                {
                    input_state = Input_State::PRESSED;
                }
            }

            Event event = {};
            event.type = Event_Type::KEY;
            event.key = key_code;
            event.is_control_down = is_left_control_down || is_right_control_down;
            event.is_shift_down = is_left_shift_down || is_right_shift_down;
            event.pressed = input_state == Input_State::PRESSED;
            event.held = input_state == Input_State::HELD;
            engine->input.key_states[key_code] = input_state;
            on_event(engine, event);
        } break;

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            Event event = {};
            win32_handle_mouse_input(&event, message, w_param, l_param);

            event.pressed = true;
            event.held = true;

            Input *input = &engine->input;
            input->button_states[event.button] = Input_State::PRESSED;

            on_event(engine, event);
        } break;

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        case WM_XBUTTONUP:
        {
            Event event = {};
            win32_handle_mouse_input(&event, message, w_param, l_param);

            Input *input = &engine->input;
            input->button_states[event.button] = Input_State::RELEASED;

            on_event(engine, event);
        } break;

        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_XBUTTONDBLCLK:
        {
            Event event = {};
            win32_handle_mouse_input(&event, message, w_param, l_param);
            event.double_click = true;
            on_event(engine, event);
        } break;

        case WM_NCMOUSEMOVE:
        case WM_MOUSEMOVE:
        {
            Event event = {};
            win32_handle_mouse_input(&event, message, w_param, l_param);
            on_event(engine, event);
        } break;

        case WM_MOUSEWHEEL:
        {
            S32 delta = u64_to_s32(w_param >> 16);
            win32_platform_state.mouse_wheel_accumulated_delta += delta;

            Event event = {};
            event.type = Event_Type::MOUSE;

            while (win32_platform_state.mouse_wheel_accumulated_delta >= 120)
            {
                event.mouse_wheel_up = true;
                on_event(engine, event);
                win32_platform_state.mouse_wheel_accumulated_delta -= 120;
            }

            while (win32_platform_state.mouse_wheel_accumulated_delta <= -120)
            {
                event.mouse_wheel_down = true;
                on_event(engine, event);
                win32_platform_state.mouse_wheel_accumulated_delta += 120;
            }
        } break;

        default:
        {
            result = DefWindowProc(window, message, w_param, l_param);
        } break;
    }

    ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
    return result;
}

INT WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance, PSTR command_line, INT show)
{
    (void)previous_instance;
    (void)command_line;
    (void)show;

    win32_platform_state.instance = instance;
    win32_platform_state.window_class_name = "hope_window_class";
    win32_platform_state.cursor = LoadCursor(NULL, IDC_ARROW);
    win32_platform_state.engine = (Engine *)VirtualAlloc(0, sizeof(Engine), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

    Engine *engine = win32_platform_state.engine;

    WNDCLASSA window_class = {};
    window_class.style = CS_DBLCLKS;
    window_class.lpfnWndProc = win32_window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = win32_platform_state.window_class_name;
    window_class.hCursor = NULL;
    window_class.hIcon = NULL; // todo(amer): icons
    ATOM success = RegisterClassA(&window_class);
    HE_ASSERT(success != 0);

    bool started = startup(engine);
    HE_ASSERT(started);

    engine->is_running = started;

    LARGE_INTEGER performance_frequency;
    HE_ASSERT(QueryPerformanceFrequency(&performance_frequency));
    S64 counts_per_second = performance_frequency.QuadPart;

    LARGE_INTEGER last_counter;
    HE_ASSERT(QueryPerformanceCounter(&last_counter));

    Win32_Window_State *win32_window_state = (Win32_Window_State *)engine->window.platform_window_state;
    HWND window_handle = win32_window_state->handle;

    while (engine->is_running)
    {
        SleepEx(0, TRUE);

        LARGE_INTEGER current_counter;
        HE_ASSERT(QueryPerformanceCounter(&current_counter));

        S64 elapsed_counts = current_counter.QuadPart - last_counter.QuadPart;
        F64 elapsed_time = (F64)elapsed_counts / (F64)counts_per_second;
        last_counter = current_counter;
        F32 delta_time = (F32)elapsed_time;

        MSG message = {};
        while (PeekMessageA(&message, window_handle, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);        
        }

        Input *input = &engine->input;

        RECT window_rect;
        GetWindowRect(window_handle, &window_rect);

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

//
// memory
//

U64 platform_get_total_memory_size()
{
    U64 size = 0;
    GetPhysicallyInstalledSystemMemory(&size);
    return size * 1024;
}

void* platform_allocate_memory(U64 size)
{
    HE_ASSERT(size);
    return VirtualAlloc(0, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
}

void* platform_reserve_memory(U64 size)
{
    HE_ASSERT(size);
    return VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool platform_commit_memory(void *memory, U64 size)
{
    HE_ASSERT(memory);
    HE_ASSERT(size);
    void *result = VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE);
    return result != nullptr;
}

void platform_deallocate_memory(void *memory)
{
    HE_ASSERT(memory);
    VirtualFree(memory, 0, MEM_RELEASE);
}

//
// window
//

bool platform_create_window(Window *window, const char *title, U32 width, U32 height, bool maximized, Window_Mode window_mode)
{
    HWND window_handle = CreateWindowExA(0, win32_platform_state.window_class_name, title,
                                         WS_OVERLAPPEDWINDOW,
                                         CW_USEDEFAULT, CW_USEDEFAULT,
                                         width,
                                         height,
                                         NULL, NULL, win32_platform_state.instance, nullptr);
    if (window_handle == NULL)
    {
        // todo(amer): logging
        return false;
    }

    RECT screen = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &screen, 0);

    S32 screen_width = screen.right - screen.left;
    S32 screen_height = screen.bottom - screen.top;

    S32 center_x = screen.left + (S32)((screen_width / 2.0f) - (width / 2.0f));
    S32 center_y = screen.top + (S32)((screen_height / 2.0f) - (height / 2.0f));

    SetWindowPos(window_handle, HWND_TOP, center_x, center_y, width, height, SWP_SHOWWINDOW);

    if (maximized)
    {
        ShowWindow(window_handle, SW_MAXIMIZE);
    }

    Win32_Window_State *win32_window_state = (Win32_Window_State *)VirtualAlloc(0, sizeof(Win32_Window_State), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    win32_window_state->handle = window_handle;

    window->platform_window_state = win32_window_state;
    window->mode = Window_Mode::WINDOWED;
    window->title = title;
    window->width = width;
    window->height = height;

    platform_set_window_mode(window, window_mode);
    return true;
}

void platform_set_window_mode(Window *window, Window_Mode window_mode)
{
    if (window->mode == window_mode)
    {
        return;
    }
    window->mode = window_mode;

    Win32_Window_State *win32_window_state = (Win32_Window_State *)window->platform_window_state;
    HWND window_handle = win32_window_state->handle;
    WINDOWPLACEMENT *window_placement_before_fullscreen = &win32_window_state->placement_before_fullscreen;

    DWORD style = GetWindowLong(window_handle, GWL_STYLE);

    if (window_mode == Window_Mode::FULLSCREEN)
    {
        HE_ASSERT((style & WS_OVERLAPPEDWINDOW));

        MONITORINFO monitor_info = { sizeof(MONITORINFO) };
        HMONITOR monitor = MonitorFromWindow(window_handle, MONITOR_DEFAULTTOPRIMARY);

        if (GetWindowPlacement(window_handle, window_placement_before_fullscreen) && GetMonitorInfo(monitor, &monitor_info))
        {
            SetWindowLong(window_handle, GWL_STYLE, style & ~(WS_OVERLAPPEDWINDOW));
            SetWindowPos(window_handle, HWND_TOP,
                         monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.top,
                         monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                         monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                         SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        }
    }
    else if (window_mode == Window_Mode::WINDOWED)
    {
        SetWindowLong(window_handle, GWL_STYLE, style|WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window_handle, window_placement_before_fullscreen);
        SetWindowPos(window_handle, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
    }
}

static void convert_filer_and_extensions_to_win32_format(char *buffer, const char *filter, U64 filter_count, const char **extensions, U32 extension_count)
{
    char *current = buffer;

    if (filter_count && extension_count)
    {
        current += sprintf(current, "%.*s", u64_to_u32(filter_count), filter) + 1;

        for (U32 i = 0; i < extension_count - 1; i++)
        {
            current += sprintf(current, "*.%s;", extensions[i]);
        }

        current += sprintf(current, "*.%s\0\0", extensions[extension_count - 1]);
    }
}

bool platform_open_file_dialog(char *buffer, U64 count, const char *title, U64 title_count, const char *filter, U64 filter_count, const char **extensions, U32 extension_count)
{
    char filter_buffer[4096] = {};
    convert_filer_and_extensions_to_win32_format(filter_buffer, filter, filter_count, extensions, extension_count);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = buffer;
    // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
    // use the contents of szFile to initialize itself.
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = u64_to_u32(count);
    ofn.lpstrFilter = filter_buffer;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) != TRUE)
    {
        return false;
    }
    return true;
}

bool platform_save_file_dialog(char *buffer, U64 count, const char *title, U64 title_count, const char *filter, U64 filter_count, const char **extensions, U32 extension_count)
{
    char filter_buffer[4096] = {};
    convert_filer_and_extensions_to_win32_format(filter_buffer, filter, filter_count, extensions, extension_count);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = buffer;
    // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
    // use the contents of szFile to initialize itself.
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = u64_to_u32(count);
    ofn.lpstrFilter = filter_buffer;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn) != TRUE)
    {
        return false;
    }
    return true;
}

//
// files
//

bool platform_path_exists(const char *path, bool *is_file)
{
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    
    if (is_file)
    {
        *is_file = !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    
    return true;
}

U64 platform_get_file_last_write_time(const char *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    BOOL result = GetFileAttributesExA(path, GetFileExInfoStandard, &data);
    HE_ASSERT(result != 0);
    ULARGE_INTEGER last_write_time = {};
    last_write_time.LowPart = data.ftLastWriteTime.dwLowDateTime;
    last_write_time.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return last_write_time.QuadPart;
}

bool platform_get_current_working_directory(char *buffer, U64 size, U64 *out_count)
{
    HE_ASSERT(buffer);
    HE_ASSERT(size);
    HE_ASSERT(out_count);
    DWORD count = GetCurrentDirectoryA(u64_to_u32(size), buffer);
    if (count + 1 > size)
    {
        return false;
    }
    buffer[count] = '\0';
    *out_count = count;
    return true;
}

void platform_walk_directory(const char *path, bool recursive, on_walk_directory_proc on_walk_directory)
{
    char path_buffer[MAX_PATH];
    sprintf(path_buffer, "%s\\*", path);    

    WIN32_FIND_DATAA find_data = {};
    HANDLE handle = FindFirstFileA(path_buffer, &find_data);

    if (handle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    
    do
    {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
        {
            continue;
        }
        
        S32 count = sprintf(path_buffer, "%s/%s", path, find_data.cFileName);
        String path = { (U64)count, path_buffer };
        
        bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        on_walk_directory(&path, is_directory);

        if (recursive && is_directory)
        {
            platform_walk_directory(path_buffer, recursive, on_walk_directory);
        }
    }
    while (FindNextFileA(handle, &find_data));

    FindClose(handle);
}

Open_File_Result platform_open_file(const char *filepath, Open_File_Flags open_file_flags)
{
    Open_File_Result result = {};

    DWORD access_flags = 0;
    DWORD creation_disposition = OPEN_ALWAYS;

    if ((open_file_flags & OpenFileFlag_Read) && (open_file_flags & OpenFileFlag_Write))
    {
        access_flags = GENERIC_READ|GENERIC_WRITE;
    }
    else if ((open_file_flags & OpenFileFlag_Read))
    {
        access_flags = GENERIC_READ;
        creation_disposition = OPEN_EXISTING;
    }
    else if ((open_file_flags & OpenFileFlag_Write))
    {
        access_flags = GENERIC_WRITE;
    }

    if ((open_file_flags & OpenFileFlag_Truncate))
    {
        creation_disposition = CREATE_ALWAYS;
    }

    HANDLE file_handle = CreateFileA(filepath, access_flags, FILE_SHARE_READ|FILE_SHARE_WRITE, 0, creation_disposition, FILE_ATTRIBUTE_NORMAL, NULL);

    if (file_handle == INVALID_HANDLE_VALUE)
    {
        win32_log_last_error();
    }
    else
    {
        LARGE_INTEGER file_size = {};
        BOOL success = GetFileSizeEx(file_handle, &file_size);
        HE_ASSERT(success);

        result.handle = file_handle;
        result.size = file_size.QuadPart;
        result.success = true;
    }

    return result;
}

bool platform_read_data_from_file(const Open_File_Result *open_file_result, U64 offset, void *data, U64 size)
{
    HE_ASSERT(open_file_result->handle != INVALID_HANDLE_VALUE);

    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // todo(amer): we are only limited to a read of 4GBs
    DWORD read_bytes;
    BOOL result = ReadFile(open_file_result->handle, data,
                           u64_to_u32(size), &read_bytes, &overlapped);
    return result == TRUE && read_bytes == size;
}

bool platform_write_data_to_file(const Open_File_Result *open_file_result, U64 offset, void *data, U64 size)
{
    HE_ASSERT(open_file_result->handle != INVALID_HANDLE_VALUE);

    OVERLAPPED overlapped = {};
    overlapped.Offset = u64_to_u32(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = u64_to_u32(offset >> 32);

    // todo(amer): we are only limited to a write of 4GBs
    DWORD written_bytes;
    BOOL result = WriteFile(open_file_result->handle, data,
                            u64_to_u32(size), &written_bytes, &overlapped);
    return result == TRUE && written_bytes == size;
}

bool platform_close_file(Open_File_Result *open_file_result)
{
    HE_ASSERT(open_file_result->handle != INVALID_HANDLE_VALUE);
    bool result = CloseHandle(open_file_result->handle) != 0;
    open_file_result->handle = nullptr;
    return result;
}

struct Watch_Directory_Info
{
    HANDLE                  directory_handle;
    void                   *buffer;
    U64                     buffer_size;
    on_watch_directory_proc on_watch_directory;
};

static void overlapped_completion(DWORD error_code, DWORD number_of_bytes_transfered, LPOVERLAPPED overlapped)
{
    if (!number_of_bytes_transfered)
    {
        win32_log_last_error();
        return;
    }

    Watch_Directory_Info *watch_directory_info = (Watch_Directory_Info *)overlapped->Pointer;

    U64 offset = 0;
    FILE_NOTIFY_INFORMATION *file_info = nullptr;

    char filename[256] = {};
    S32 filename_length = 0;

    char new_filename[256] = {};
    S32 new_filename_length = 0;

    do
    {
        file_info = (FILE_NOTIFY_INFORMATION *)((U8 *)watch_directory_info->buffer + offset);
        offset += file_info->NextEntryOffset;
        
        switch (file_info->Action)
        {
            case FILE_ACTION_ADDED:
            {
                WideCharToMultiByte(CP_ACP, 0, file_info->FileName, file_info->FileNameLength, filename, file_info->FileNameLength / 2, NULL, NULL);
                filename_length = file_info->FileNameLength / 2;

                String path = { .count = (U64)filename_length, .data = filename };
                watch_directory_info->on_watch_directory(Watch_Directory_Result::FILE_ADDED, path, path);
            } break;

            case FILE_ACTION_REMOVED:
            {
                WideCharToMultiByte(CP_ACP, 0, file_info->FileName, file_info->FileNameLength, filename, file_info->FileNameLength / 2, NULL, NULL);
                filename_length = file_info->FileNameLength / 2;

                String path = { .count = (U64)filename_length, .data = filename };
                watch_directory_info->on_watch_directory(Watch_Directory_Result::FILE_DELETED, path, path);
            } break;

            case FILE_ACTION_MODIFIED:
            {
                WideCharToMultiByte(CP_ACP, 0, file_info->FileName, file_info->FileNameLength, filename, file_info->FileNameLength / 2, NULL, NULL);
                filename_length = file_info->FileNameLength / 2;

                String path = { .count = (U64)filename_length, .data = filename };
                watch_directory_info->on_watch_directory(Watch_Directory_Result::FILE_MODIFIED, path, path);
            } break;

            case FILE_ACTION_RENAMED_OLD_NAME:
            {
                WideCharToMultiByte(CP_ACP, 0, file_info->FileName, file_info->FileNameLength, filename, file_info->FileNameLength / 2, NULL, NULL);
                filename_length = file_info->FileNameLength / 2;
            } break;

            case FILE_ACTION_RENAMED_NEW_NAME:
            {
                WideCharToMultiByte(CP_ACP, 0, file_info->FileName, file_info->FileNameLength, new_filename, file_info->FileNameLength / 2, NULL, NULL);
                new_filename_length = file_info->FileNameLength / 2;

                String old_path = { .count = (U64)filename_length,     .data = filename };
                String new_path = { .count = (U64)new_filename_length, .data = new_filename };
                watch_directory_info->on_watch_directory(Watch_Directory_Result::FILE_RENAMED, old_path, new_path);
            } break;
        }
    }
    while (file_info->NextEntryOffset);
    
    DWORD filters = FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE;
    if (!ReadDirectoryChangesW(watch_directory_info->directory_handle, watch_directory_info->buffer, u64_to_u32(watch_directory_info->buffer_size), TRUE, filters, 0, overlapped, &overlapped_completion))
    {
        win32_log_last_error();
    }
}

bool platform_watch_directory(const char *path, on_watch_directory_proc on_watch_directory)
{
    HANDLE handle = CreateFile(path, GENERIC_READ|FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED, 0);
    
    if (handle == INVALID_HANDLE_VALUE)
    {
        win32_log_last_error();
        return false;
    }
    
    U64 buffer_size = 4096;
    U64 total_size = buffer_size + sizeof(Watch_Directory_Info) + sizeof(OVERLAPPED);
    
    void *memory = VirtualAlloc(NULL, total_size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!memory)
    {
        win32_log_last_error();
        return false;
    }

    void *buffer = memory;
    Watch_Directory_Info *info = (Watch_Directory_Info *)((U8 *)memory + buffer_size);
    info->directory_handle = handle;
    info->buffer = buffer;
    info->buffer_size = buffer_size;
    info->on_watch_directory = on_watch_directory;
    
    DWORD filters = FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_LAST_WRITE;
    
    OVERLAPPED *overlapped = (OVERLAPPED *)((U8*)memory + buffer_size + sizeof(Watch_Directory_Info));
    overlapped->Pointer = info;
    
    if (!ReadDirectoryChangesW(handle, buffer, u64_to_u32(buffer_size), TRUE, filters, 0, overlapped, &overlapped_completion))
    {
        win32_log_last_error();
        return false;
    }
    
    return true;
}

//
// dynamic library
//

bool platform_load_dynamic_library(Dynamic_Library *dynamic_library, const char *filepath)
{
    DWORD flags = DONT_RESOLVE_DLL_REFERENCES|LOAD_IGNORE_CODE_AUTHZ_LEVEL;
    HMODULE library_handle = LoadLibraryExA(filepath, NULL, flags);
    if (library_handle == NULL)
    {
        return false;
    }
    dynamic_library->platform_dynamic_library_state = library_handle;
    return true;
}

void *platform_get_proc_address(Dynamic_Library *dynamic_library, const char *proc_name)
{
    HE_ASSERT(dynamic_library->platform_dynamic_library_state);
    return GetProcAddress((HMODULE)dynamic_library->platform_dynamic_library_state, proc_name);
}

bool platform_unload_dynamic_library(Dynamic_Library *dynamic_library)
{
    HE_ASSERT(dynamic_library->platform_dynamic_library_state);
    return FreeLibrary((HMODULE)dynamic_library->platform_dynamic_library_state) == 0;
}

//
// vulkan
//

void* platform_create_vulkan_surface(Engine *engine, void *instance, const void *allocator_callbacks)
{
    Win32_Window_State *win32_window_state = (Win32_Window_State *)engine->window.platform_window_state;
    VkWin32SurfaceCreateInfoKHR surface_create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surface_create_info.hinstance = win32_platform_state.instance;
    surface_create_info.hwnd = (HWND)win32_window_state->handle;

    VkSurfaceKHR surface = 0;
    VkResult result = vkCreateWin32SurfaceKHR((VkInstance)instance, &surface_create_info, (VkAllocationCallbacks *)allocator_callbacks, &surface);
    HE_ASSERT(result == VK_SUCCESS);
    return surface;
}

//
// threading
//

bool platform_create_and_start_thread(Thread *thread, Thread_Proc thread_proc, void *params, const char *name)
{
    HE_ASSERT(thread);
    HE_ASSERT(thread_proc);

    DWORD thread_id;
    HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, params, 0, &thread_id);
    if (thread_handle == NULL)
    {
        return false;
    }

#ifndef HE_SHIPPING

    if (name)
    {
        wchar_t wide_name[256];
        U64 count = string_length(name);
        HE_ASSERT(count <= 255);

        int result = MultiByteToWideChar(CP_OEMCP, 0, name, -1, wide_name, u64_to_u32(count + 1));
        HE_ASSERT(result);

        HRESULT hresult = SetThreadDescription(thread_handle, wide_name);
        HE_ASSERT(!FAILED(hresult));
    }

#endif

    thread->platform_thread_state = thread_handle;
    return true;
}

U32 platform_get_thread_count()
{
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors;
}

U32 platform_get_current_thread_id()
{
    return *(U32 *)( (U8 *)__readgsqword(0x30) + 0x48 );
}

U32 platform_get_thread_id(Thread *thread)
{
    return GetThreadId((HANDLE)thread->platform_thread_state);
}

bool platform_create_mutex(Mutex *mutex)
{
    CRITICAL_SECTION *critical_section = (CRITICAL_SECTION *)VirtualAlloc(0, sizeof(CRITICAL_SECTION), MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    if (!critical_section)
    {
        return false;
    }
    InitializeCriticalSection(critical_section);
    mutex->platform_mutex_state = critical_section;
    return true;
}

void platform_lock_mutex(Mutex *mutex)
{
    CRITICAL_SECTION *critical_section = (CRITICAL_SECTION *)mutex->platform_mutex_state;
    EnterCriticalSection(critical_section);
}

void platform_unlock_mutex(Mutex *mutex)
{
    CRITICAL_SECTION *critical_section = (CRITICAL_SECTION *)mutex->platform_mutex_state;
    LeaveCriticalSection(critical_section);
}

void platform_wait_for_mutexes(Mutex *mutexes, U32 mutex_count)
{
    WaitForMultipleObjects(mutex_count, (HANDLE *)mutexes, true, INFINITE);
}

bool platform_create_semaphore(Semaphore *semaphore, U32 init_count)
{
    HANDLE semaphore_handle = CreateSemaphoreA(0, init_count, LONG_MAX, NULL);
    if (semaphore_handle == NULL)
    {
        return false;
    }
    semaphore->platform_semaphore_state = semaphore_handle;
    return true;
}

bool platform_signal_semaphore(Semaphore *semaphore, U32 increase_amount)
{
    HANDLE semaphore_handle = (HANDLE)semaphore->platform_semaphore_state;
    BOOL result = ReleaseSemaphore(semaphore_handle, increase_amount, NULL);
    return result != 0;
}

bool platform_wait_for_semaphore(Semaphore *semaphore)
{
    HANDLE semaphore_handle = (HANDLE)semaphore->platform_semaphore_state;
    DWORD result = WaitForSingleObject(semaphore_handle, INFINITE);
    return result == WAIT_OBJECT_0;
}

//
// imgui
//

static int imgui_platform_create_vk_surface(ImGuiViewport *vp, ImU64 vk_inst, const void *vk_allocators, ImU64 *out_vk_surface)
{
    ImGui_ImplWin32_ViewportData *viewport_data = (ImGui_ImplWin32_ViewportData *)vp->PlatformUserData;

    VkWin32SurfaceCreateInfoKHR surface_create_info = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surface_create_info.hinstance = win32_platform_state.instance;
    surface_create_info.hwnd = viewport_data->Hwnd;

    VkSurfaceKHR surface = 0;
    VkResult result = vkCreateWin32SurfaceKHR((VkInstance)vk_inst, &surface_create_info, (VkAllocationCallbacks *)vk_allocators, (VkSurfaceKHR *)out_vk_surface);
    HE_ASSERT(result == VK_SUCCESS);

    return (int)result;
}

void platform_init_imgui(struct Engine *engine)
{
    Win32_Window_State *win32_window_state = (Win32_Window_State *)engine->window.platform_window_state;
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_CreateVkSurface = imgui_platform_create_vk_surface;
    ImGui_ImplWin32_Init(win32_window_state->handle);
}

void platform_imgui_new_frame()
{
    ImGui_ImplWin32_NewFrame();
}

void platform_shutdown_imgui()
{
    ImGui_ImplWin32_Shutdown();
}

//
// debugging
//
void platform_debug_printf(const char *message)
{
    OutputDebugStringA(message);
}

//
// misc
//

bool platform_execute_command(const char *command)
{
    S32 result = system(command);
    return result != -1;
}