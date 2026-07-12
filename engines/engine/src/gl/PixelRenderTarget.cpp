#include "gl/PixelRenderTarget.h"

#include "gl/ShaderUtils.h"

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
Grade sGrade;

// Fullscreen-quad blit through a fragment shader that applies the color grade.
// Replaces glBlitFramebuffer so the whole frame can be graded (saturation /
// brightness / tint) at upscale time -- the post-process seam.
GLuint sProgram = 0;
GLuint sVao = 0;
GLuint sVbo = 0;
GLint sLocSaturation = -1;
GLint sLocBrightness = -1;
GLint sLocTint = -1;

const char* kVert = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;   // clip-space quad [-1,1]
layout(location = 1) in vec2 aUv;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* kFrag = R"glsl(
#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTex;
uniform float uSaturation;
uniform float uBrightness;
uniform vec3 uTint;
void main() {
    vec3 c = texture(uTex, vUv).rgb;
    // Saturation: lerp toward luminance (Rec.601).
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, uSaturation);
    c *= uBrightness;
    c *= uTint;
    FragColor = vec4(c, 1.0);
}
)glsl";

void initBlitResources()
{
    sProgram = compileProgram(kVert, kFrag);
    sLocSaturation = glGetUniformLocation(sProgram, "uSaturation");
    sLocBrightness = glGetUniformLocation(sProgram, "uBrightness");
    sLocTint = glGetUniformLocation(sProgram, "uTint");

    // A fullscreen quad. Position filled per-draw from the blit rect (so the
    // upscaled image lands centered with letterbox); UVs are static.
    glGenVertexArrays(1, &sVao);
    glGenBuffers(1, &sVbo);
    glBindVertexArray(sVao);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
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

void pixelTargetSetGrade(const Grade& grade)
{
    sGrade = grade;
}

Grade pixelTargetGetGrade()
{
    return sGrade;
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

    initBlitResources();
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
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);

    // Convert the centered blit rect (pixels, top-left origin) to clip space.
    const float w = static_cast<float>(windowW);
    const float h = static_cast<float>(windowH);
    const float x0 = static_cast<float>(sBlit.x) / w * 2.0f - 1.0f;
    const float x1 = static_cast<float>(sBlit.x + sBlit.width) / w * 2.0f - 1.0f;
    // Flip Y: window top-left maps to clip top (+1); the FBO texture's V is
    // bottom-up, so top of the image samples v=1.
    const float y0 = 1.0f - static_cast<float>(sBlit.y) / h * 2.0f;
    const float y1 = 1.0f - static_cast<float>(sBlit.y + sBlit.height) / h * 2.0f;

    const float verts[6 * 4] = {
        x0, y0, 0.0f, 1.0f, x1, y0, 1.0f, 1.0f, x1, y1, 1.0f, 0.0f,
        x0, y0, 0.0f, 1.0f, x1, y1, 1.0f, 0.0f, x0, y1, 0.0f, 0.0f,
    };

    glUseProgram(sProgram);
    glUniform1f(sLocSaturation, sGrade.saturation);
    glUniform1f(sLocBrightness, sGrade.brightness);
    glUniform3f(sLocTint, sGrade.tint_r, sGrade.tint_g, sGrade.tint_b);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sColorTex);

    glBindVertexArray(sVao);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void pixelTargetShutdown()
{
    if (sColorTex)
        glDeleteTextures(1, &sColorTex);
    if (sFbo)
        glDeleteFramebuffers(1, &sFbo);
    if (sProgram)
        glDeleteProgram(sProgram);
    if (sVbo)
        glDeleteBuffers(1, &sVbo);
    if (sVao)
        glDeleteVertexArrays(1, &sVao);
    sColorTex = sFbo = sProgram = sVbo = sVao = 0;
}

} // namespace engine::gl
