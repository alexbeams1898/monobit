#include "gl/PixelRenderTarget.h"

#include <algorithm>
#include <iostream>

#include <glad/glad.h>

namespace engine::gl
{

namespace
{
GLuint sFbo = 0;
GLuint sColorTex = 0;
int sInternalW = 0;
int sInternalH = 0;
BlitRect sBlit;
} // namespace

BlitRect computeBlitRect(int internalW, int internalH, int windowW, int windowH)
{
    BlitRect rect;
    if (internalW <= 0 || internalH <= 0)
        return rect;

    const int scale = std::max(1, std::min(windowW / internalW, windowH / internalH));
    rect.scale = scale;
    rect.width = internalW * scale;
    rect.height = internalH * scale;
    rect.x = (windowW - rect.width) / 2; // centered; remainder is letterbox
    rect.y = (windowH - rect.height) / 2;
    return rect;
}

void pixelTargetInit(int internalW, int internalH)
{
    sInternalW = internalW;
    sInternalH = internalH;

    glGenFramebuffers(1, &sFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);

    glGenTextures(1, &sColorTex);
    glBindTexture(GL_TEXTURE_2D, sColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, internalW, internalH, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 nullptr);
    // Nearest sampling on both axes -- the whole point of the pixel-art path.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[PixelRenderTarget] framebuffer incomplete\n";

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void pixelTargetResize(int windowW, int windowH)
{
    sBlit = computeBlitRect(sInternalW, sInternalH, windowW, windowH);
}

void pixelTargetBegin(float r, float g, float b)
{
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);
    glViewport(0, 0, sInternalW, sInternalH);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void pixelTargetEnd(int windowW, int windowH)
{
    (void)windowW;
    (void)windowH;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, sInternalW, sInternalH, sBlit.x, sBlit.y, sBlit.x + sBlit.width,
                      sBlit.y + sBlit.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void pixelTargetShutdown()
{
    if (sColorTex)
        glDeleteTextures(1, &sColorTex);
    if (sFbo)
        glDeleteFramebuffers(1, &sFbo);
    sColorTex = 0;
    sFbo = 0;
}

} // namespace engine::gl
