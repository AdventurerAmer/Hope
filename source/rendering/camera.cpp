#include "camera.h"

void calculate_view_matrix(Camera *camera)
{
    // note(amer): could we inverse the quaternion then convert it to a matrix
    // we are doing this for now which is better then using glm::inverse
    glm::mat4 inverse_roation = glm::transpose(glm::toMat4(camera->rotation));
    glm::mat4 inverse_translation = glm::translate(glm::mat4(1.0f), -camera->position);
    camera->view = inverse_roation * inverse_translation;
}

void calculate_projection_matrix(Camera *camera)
{
    camera->projection = glm::perspective(glm::radians(camera->field_of_view), camera->aspect_ratio, camera->near_clip, camera->far_clip);
}

void init_camera(Camera *camera, glm::vec3 position,
                 glm::quat rotation, F32 aspect_ratio,
                 F32 field_of_view /*= 45.0f*/, F32 near_clip /*= 0.1f */, F32 far_clip /*= 1000.0f */)
{
    camera->position = position;
    camera->rotation = rotation;
    camera->aspect_ratio = aspect_ratio;
    camera->field_of_view = field_of_view;
    camera->near_clip = near_clip;
    camera->far_clip = far_clip;

    calculate_view_matrix(camera);
    calculate_projection_matrix(camera);
}

void update_camera(Camera *camera)
{
    calculate_view_matrix(camera);
    calculate_projection_matrix(camera);
}

void init_fps_camera_controller(FPS_Camera_Controller *camera_controller,
                                F32 pitch, F32 yaw, F32 rotation_speed /*= 45.0f*/,
                                F32 base_movement_speed /*=15.0f*/,
                                F32 max_movement_speed /*=35.0f*/,
                                F32 sensitivity_x /*=1.0f*/,
                                F32 sensitivity_y /*=1.0f*/)
{
    camera_controller->pitch = pitch;
    camera_controller->yaw   = yaw;
    camera_controller->rotation_speed = rotation_speed;
    camera_controller->base_movement_speed = base_movement_speed;
    camera_controller->max_movement_speed = max_movement_speed;
    camera_controller->sensitivity_x = sensitivity_y;
    camera_controller->sensitivity_y = sensitivity_y;
}

void control_camera(FPS_Camera_Controller *controller,
                    Camera *camera,
                    const FPS_Camera_Controller_Input input,
                    F32 delta_time)
{
    controller->yaw += input.delta_x * controller->sensitivity_x * controller->rotation_speed * delta_time;

    while (controller->yaw >= 360.0f)
    {
        controller->yaw -= 360.0f;
    }

    while (controller->yaw <= -360.0f)
    {
        controller->yaw += 360.0f;
    }

    controller->pitch += input.delta_y * controller->sensitivity_y * controller->rotation_speed * delta_time;
    controller->pitch = glm::clamp(controller->pitch, -89.0f, 89.0f);

    glm::quat camera_rotation = glm::quat({ glm::radians(controller->pitch), glm::radians(controller->yaw), 0.0f });
    glm::vec3 forward = glm::rotate(camera_rotation, glm::vec3(0.0f, 0.0f, -1.0f));
    glm::vec3 right = glm::rotate(camera_rotation, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 up = glm::rotate(camera_rotation, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 movement_direction = {};

    F32 movement_speed = controller->base_movement_speed;

    if (input.move_fast)
    {
        movement_speed = controller->max_movement_speed;
    }

    if (input.forward)
    {
        movement_direction += forward;
    }

    if (input.backward)
    {
        movement_direction -= forward;
    }

    if (input.right)
    {
        movement_direction += right;
    }

    if (input.left)
    {
        movement_direction -= right;
    }

    if (input.up)
    {
        movement_direction += up;
    }

    if (input.down)
    {
        movement_direction -= up;
    }

    if (glm::length2(movement_direction) > 0.1f)
    {
        movement_direction = glm::normalize(movement_direction);
    }

    camera->position += movement_direction * movement_speed * delta_time;
    camera->rotation = camera_rotation;
    update_camera(camera);
}