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
                                Camera *camera)
{
    glm::vec3 euler_angles = glm::eulerAngles(camera->rotation);
    camera_controller->pitch = euler_angles.x;
    camera_controller->yaw = euler_angles.y;
}