#include "render/Camera.h"

namespace selva::render
{

namespace
{
float sCamYaw = 0.0f;
float sCamPitch = -0.25f;
int sWindowW = 0;
int sWindowH = 0;
glm::mat4 sLastViewProj{1.0f};
CameraMode sCamMode = CameraMode::ThirdPerson;
} // namespace

float cameraYaw()
{
    return sCamYaw;
}

float cameraPitch()
{
    return sCamPitch;
}

void setCameraYaw(float yaw)
{
    sCamYaw = yaw;
}

void setCameraPitch(float pitch)
{
    sCamPitch = pitch;
}

CameraMode cameraMode()
{
    return sCamMode;
}

void setCameraMode(CameraMode mode)
{
    sCamMode = mode;
}

void toggleCameraMode()
{
    sCamMode = (sCamMode == CameraMode::ThirdPerson) ? CameraMode::FirstPerson
                                                    : CameraMode::ThirdPerson;
}

int windowWidth()
{
    return sWindowW;
}

int windowHeight()
{
    return sWindowH;
}

void onWindowResize(::Engine& /*engine*/, int new_w, int new_h)
{
    sWindowW = new_w;
    sWindowH = new_h;
}

void setInitialWindowSize(int w, int h)
{
    sWindowW = w;
    sWindowH = h;
}

void setLastViewProj(const glm::mat4& m)
{
    sLastViewProj = m;
}

const glm::mat4& lastViewProj()
{
    return sLastViewProj;
}

bool worldToScreen(const glm::mat4& view_proj, const glm::vec3& world_pos, glm::vec2& out_screen)
{
    const glm::vec4 clip = view_proj * glm::vec4(world_pos, 1.0f);
    if (clip.w <= 1e-4f)
        return false; // behind near plane
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float sx = (ndc.x * 0.5f + 0.5f) * static_cast<float>(sWindowW);
    const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(sWindowH);
    out_screen = glm::vec2(sx, sy);
    return true;
}

} // namespace selva::render
