#include "core/defines.h"
#include "core/engine.h"
#include "core/job_system.h"

struct Game_State
{
	Camera camera;
	FPS_Camera_Controller camera_controller;
};

static Game_State game_state;

HE_API bool init_game(Engine *engine)
{
    Render_Context render_context = engine->api.get_render_context();
    glm::vec2 viewport = { render_context.renderer_state->back_buffer_width, render_context.renderer_state->back_buffer_height };
    
    F32 aspect_ratio = viewport.x / viewport.y;
    glm::quat camera_rotation = glm::quat({ 0.0f, 0.0f, 0.0f });
    Camera *camera = &game_state.camera;
	F32 fov = 45.0f;
	F32 near = 0.1f;
	F32 far = 1000.0f;
	engine->api.init_camera(camera, { 0.0f, 0.3f, 1.0f }, camera_rotation, aspect_ratio, fov, near, far);

    FPS_Camera_Controller *camera_controller = &game_state.camera_controller;
    F32 rotation_speed = 45.0f;
    F32 base_movement_speed = 3.0f;
    F32 max_movement_speed = 5.0f;
	F32 sensitivity_x = 1.0f;
	F32 sensitivity_y = 1.0f;
    
	engine->api.init_fps_camera_controller(camera_controller, /*pitch=*/0.0f, /*yaw=*/0.0f, rotation_speed, base_movement_speed, max_movement_speed, sensitivity_x, sensitivity_y);

    return true;
}

HE_API void on_event(Engine *engine, Event event)
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
				else if (event.key == HE_KEY_F11)
				{
                    Window *window = &engine->window;
                    if (window->mode == Window_Mode::WINDOWED)
                    {
                        engine->api.set_window_mode(window, Window_Mode::FULLSCREEN);
                    }
                    else
                    {
                        engine->api.set_window_mode(window, Window_Mode::WINDOWED);
                    }
                }
                else if (event.key == HE_KEY_F10)
                {
                    engine->show_imgui = !engine->show_imgui;
                    engine->show_cursor = !engine->show_cursor;
                }
			}
		} break;

        case EventType_Resize:
        {
            if (event.width != 0 && event.height != 0)
            {
                game_state.camera.aspect_ratio = (F32)event.width / (F32)event.height;
                engine->api.update_camera(&game_state.camera);
            }
        } break;
	}
}

HE_API void on_update(Engine *engine, F32 delta_time)
{
	Input *input = &engine->input;
    
    Camera *camera = &game_state.camera;
    FPS_Camera_Controller *camera_controller = &game_state.camera_controller;

    FPS_Camera_Controller_Input camera_controller_input = {};
    camera_controller_input.can_control = input->button_states[HE_BUTTON_RIGHT] != InputState_Released && !engine->show_imgui;
    camera_controller_input.move_fast = input->key_states[HE_KEY_LEFT_SHIFT] != InputState_Released;
    camera_controller_input.forward = input->key_states[HE_KEY_W] != InputState_Released;
    camera_controller_input.backward = input->key_states[HE_KEY_S] != InputState_Released;
    camera_controller_input.left = input->key_states[HE_KEY_A] != InputState_Released;
    camera_controller_input.right = input->key_states[HE_KEY_D] != InputState_Released;
    camera_controller_input.up = input->key_states[HE_KEY_E] != InputState_Released;
    camera_controller_input.down = input->key_states[HE_KEY_Q] != InputState_Released;
    camera_controller_input.delta_x = -input->mouse_delta_x;
    camera_controller_input.delta_y = -input->mouse_delta_y;

    if (camera_controller_input.can_control)
    {
        engine->lock_cursor = true;
        engine->api.control_camera(camera_controller, camera, camera_controller_input, delta_time);
    }
    else
    {
        engine->lock_cursor = false;
    }

    if (!engine->is_minimized)
    {
        Render_Context render_context = engine->api.get_render_context();
        Scene_Data *scene_data = &render_context.renderer_state->scene_data;
        scene_data->view = camera->view;
        scene_data->projection = camera->projection;
    }
}