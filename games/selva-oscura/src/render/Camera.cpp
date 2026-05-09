#include "render/Camera.h"

namespace selva::render
{

namespace
{
float sCamYaw = 0.0f;
float sCamPitch = -0.25f;
int sWindowW = 0;
int sWindowH = 0;
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

} // namespace selva::render
