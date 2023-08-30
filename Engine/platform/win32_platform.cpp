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
#include "core/cvars.h"

#include "core/debugging.h"

#include "ImGui/backends/imgui_impl_win32.cpp"

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

static void win32_get_window_size(U32 client_width, U32 client_height, U32 *width, U32 *height)
{
    RECT rect;
    rect.left = 0;
    rect.right = client_width;
    rect.top = 0;
    rect.bottom = client_height;

    BOOL success = AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HOPE_Assert(success != 0);

    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
}

LRESULT CALLBACK
win32_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    LRESULT result = 0;

    switch (message)
    {
        case WM_CLOSE:
        {
            Event event = {};
            event.type = EventType_Close;
            win32_platform_state.engine->game_code.on_event(win32_platform_state.engine, event);
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
            Engine *engine = win32_platform_state.engine;
            Event event = {};
            event.type = EventType_Resize;

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

            U32 width = 0;
            U32 height = 0;
            win32_get_window_size(client_width, client_height, &width, &height);

            Window *window = &engine->window;
            window->width = width;
            window->height = height;

            event.width = u32_to_u16(client_width);
            event.height = u32_to_u16(client_height);
            engine->game_code.on_event(engine, event);

            if (engine->renderer.on_resize)
            {
                engine->renderer.on_resize(&engine->renderer_state,
                                           client_width,
                                           client_height);
            }
        } break;

        default:
        {
            result = DefWindowProc(window, message, w_param, l_param);
        } break;
    }

    return result;
}

HOPE_FORCE_INLINE static void
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
    HOPE_Assert(success != 0);
    
    bool started = startup(engine, &win32_platform_state);
    HOPE_Assert(started);

    engine->is_running = started;

    LARGE_INTEGER performance_frequency;
    HOPE_Assert(QueryPerformanceFrequency(&performance_frequency));
    S64 counts_per_second = performance_frequency.QuadPart;

    LARGE_INTEGER last_counter;
    HOPE_Assert(QueryPerformanceCounter(&last_counter));

    Win32_Window_State *win32_window_state = (Win32_Window_State *)engine->window.platform_window_state;
    HWND window_handle = win32_window_state->handle;

    Game_Code *game_code = &engine->game_code;

    while (engine->is_running)
    {
        LARGE_INTEGER current_counter;
        HOPE_Assert(QueryPerformanceCounter(&current_counter));

        S64 elapsed_counts = current_counter.QuadPart - last_counter.QuadPart;
        F64 elapsed_time = (F64)elapsed_counts / (F64)counts_per_second;
        last_counter = current_counter;
        F32 delta_time = (F32)elapsed_time;

        MSG message = {};
        while (PeekMessageA(&message, window_handle, 0, 0, PM_REMOVE))
        {
            // todo(amer): handle imgui input outside platform win32.
            if (engine->show_imgui)
            {
                ImGui_ImplWin32_WndProcHandler(window_handle, message.message, message.wParam, message.lParam);
            }

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
                    win32_platform_state.mouse_wheel_accumulated_delta += delta;

                    Event event = {};
                    event.type = EventType_Mouse;

                    while (win32_platform_state.mouse_wheel_accumulated_delta >= 120)
                    {
                        event.mouse_wheel_up = true;
                        game_code->on_event(engine, event);
                        win32_platform_state.mouse_wheel_accumulated_delta -= 120;
                    }

                    while (win32_platform_state.mouse_wheel_accumulated_delta <= -120)
                    {
                        event.mouse_wheel_down = true;
                        game_code->on_event(engine, event);
                        win32_platform_state.mouse_wheel_accumulated_delta += 120;
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

void* platform_allocate_memory(U64 size)
{
    HOPE_Assert(size);
    return VirtualAlloc(0, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
}

void platform_deallocate_memory(void *memory)
{
    HOPE_Assert(memory);
    VirtualFree(memory, 0, MEM_RELEASE);
}

//
// window
//

bool platform_create_window(Window *window, const char *title, U32 width, U32 height, Window_Mode window_mode)
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

    S32 screen_width = GetSystemMetrics(SM_CXSCREEN);
    S32 screen_height = GetSystemMetrics(SM_CYSCREEN);
    S32 center_x = (screen_width / 2) - (width / 2);
    S32 center_y = (screen_height / 2) - (height / 2);

    MoveWindow(window_handle, center_x, center_y, width, height, FALSE);
    ShowWindow(window_handle, SW_SHOW);

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

    Win32_Window_State *win32_window_state = (Win32_Window_State *)window->platform_window_state;
    HWND window_handle = win32_window_state->handle;
    WINDOWPLACEMENT *window_placement_before_fullscreen = &win32_window_state->placement_before_fullscreen;

    DWORD style = GetWindowLong(window_handle, GWL_STYLE);

    if (window_mode == Window_Mode::FULLSCREEN)
    {
        HOPE_Assert((style & WS_OVERLAPPEDWINDOW));

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
        SetWindowPos(window_handle,
                     NULL, 0, 0, 0, 0,
                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
    }
}

//
// files
//

bool platform_file_exists(const char *filepath)
{
    DWORD dwAttrib = GetFileAttributesA(filepath);
    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
           !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
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

    if ((open_file_flags & OpenFileFlag_Truncate) && platform_file_exists(filepath))
    {
        creation_disposition = TRUNCATE_EXISTING;
    }

    HANDLE file_handle = CreateFileA(filepath, access_flags, FILE_SHARE_READ,
                                     0, creation_disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (file_handle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER file_size = {};
        BOOL success = GetFileSizeEx(file_handle, &file_size);
        HOPE_Assert(success);

        result.handle = file_handle;
        result.size = file_size.QuadPart;
        result.success = true;
    }
    
    return result;
}

bool platform_read_data_from_file(const Open_File_Result *open_file_result, U64 offset, void *data, U64 size)
{
    HOPE_Assert(open_file_result->handle != INVALID_HANDLE_VALUE);

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
    HOPE_Assert(open_file_result->handle != INVALID_HANDLE_VALUE);

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
    HOPE_Assert(open_file_result->handle != INVALID_HANDLE_VALUE);
    bool result = CloseHandle(open_file_result->handle) != 0;
    open_file_result->handle = nullptr;
    return result;
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
    HOPE_Assert(dynamic_library->platform_dynamic_library_state);
    return GetProcAddress((HMODULE)dynamic_library->platform_dynamic_library_state, proc_name);
}

bool platform_unload_dynamic_library(Dynamic_Library *dynamic_library)
{
    HOPE_Assert(dynamic_library->platform_dynamic_library_state);
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
    HOPE_Assert(result == VK_SUCCESS);
    return surface;
}

//
// threading
//

bool platform_create_and_start_thread(Thread *thread, Thread_Proc thread_proc, void *params, const char *name)
{
    HOPE_Assert(thread);
    HOPE_Assert(thread_proc);

    DWORD thread_id;
    HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, params, 0, &thread_id);
    if (thread_handle == NULL)
    {
        return false;
    }

    if (name)
    {
        wchar_t wide_name[256];
        U64 count = string_length(name);
        HOPE_Assert(count <= 255);

        int result = MultiByteToWideChar(CP_OEMCP, 0, name, -1, wide_name, u64_to_u32(count + 1));
        HOPE_Assert(result);

        HRESULT hresult = SetThreadDescription(thread_handle, wide_name);
        HOPE_Assert(!FAILED(hresult));
    }

    thread->platform_thread_state = thread_handle;
    return true;
}

U32 platform_get_thread_count()
{
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    return system_info.dwNumberOfProcessors;
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

bool platform_create_semaphore(Semaphore *semaphore, U32 initial_count, const char *name)
{
    // todo(amer): not sure about the max_count
    HANDLE semaphore_handle = CreateSemaphoreA(0, initial_count, HOPE_MAX_U16, name);
    if (semaphore_handle == NULL)
    {
        return false;
    }
    semaphore->platform_semaphore_state = semaphore_handle;
    return true;
}

bool signal_semaphore(Semaphore *semaphore, U32 count)
{
    HANDLE semaphore_handle = (HANDLE)semaphore->platform_semaphore_state;
    BOOL result = ReleaseSemaphore(semaphore_handle, count, NULL);
    return result;
}

bool wait_for_semaphore(Semaphore *semaphore)
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
    HOPE_Assert(result == VK_SUCCESS);

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