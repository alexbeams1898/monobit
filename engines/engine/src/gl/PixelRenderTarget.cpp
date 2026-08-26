#include "gl/PixelRenderTarget.h"

#include "gl/ShaderUtils.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glad/glad.h>

namespace engine::gl
{

namespace
{
GLuint sFbo = 0;
GLuint sColorTex = 0;
// Base = the game's LOGICAL render resolution (the FOV the camera projects); it
// never changes. Render = the FBO's actual pixel size = base * supersample factor,
// chosen per window so the final blit DOWNSCALES (crisp) instead of upscaling
// (soft). At an exact integer window (1440p/4K) the factor makes render == window
// and the blit is 1:1. sInternalW/H are the RENDER size (what the FBO + viewport
// use); base is kept separately for the factor math.
int sBaseW = 0;
int sBaseH = 0;
int sInternalW = 0; // render (FBO) width  = sBaseW * factor
int sInternalH = 0; // render (FBO) height = sBaseH * factor
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
GLint sLocTexSize = -1;

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
uniform vec2 uTexSize;   // internal render resolution (texels)
uniform float uSaturation;
uniform float uBrightness;
uniform vec3 uTint;
void main() {
    // "Sharp bilinear": crisp pixel-art scaling at ANY (incl. fractional) ratio.
    // Snap the sample toward each texel's center, but ramp across the seam over
    // exactly one screen pixel's worth of texels (fwidth) -- so the interior of a
    // texel is flat (nearest-crisp) and only the 1px seam is bilinear-smoothed.
    // Fills the screen edge-to-edge with no shimmer and no visible blur.
    vec2 px = vUv * uTexSize;
    vec2 seam = floor(px) + 0.5;
    vec2 ramp = clamp((px - seam) / fwidth(px), -0.5, 0.5) + 0.5;
    vec2 uv = (floor(px) + ramp) / uTexSize;
    vec3 c = texture(uTex, uv).rgb;
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
    sLocTexSize = glGetUniformLocation(sProgram, "uTexSize");

    // A fullscreen quad. Position filled per-draw from the blit rect (so the
    // upscaled image lands centered with letterbox); UVs are static.
    glGenVertexArrays(1, &sVao);
    glGenBuffers(1, &sVbo);
    glBindVertexArray(sVao);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(6 * 4) * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);
    // The attribute's offset is passed as a pointer for historical reasons: it is a byte offset
    // into the bound buffer, never an address to dereference.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
} // namespace

BlitRect computeBlitRect(int internalW, int internalH, int windowW, int windowH)
{
    BlitRect rect;
    if (internalW <= 0 || internalH <= 0)
        return rect;

    // Aspect-fit the internal image into the window at the largest FRACTIONAL scale
    // (the sharp-bilinear blit shader keeps pixels crisp at any ratio -- so we fill
    // the screen instead of integer-flooring). Only the ASPECT remainder letterboxes
    // (e.g. a 16:9 image on a 16:10 or ultrawide panel), never an integer remainder.
    const float sx = static_cast<float>(windowW) / static_cast<float>(internalW);
    const float sy = static_cast<float>(windowH) / static_cast<float>(internalH);
    const float scale = std::min(sx, sy);
    rect.scale = std::max(1, static_cast<int>(scale)); // informational (diagnostics)
    rect.width = static_cast<int>(std::lround(static_cast<float>(internalW) * scale));
    rect.height = static_cast<int>(std::lround(static_cast<float>(internalH) * scale));
    rect.x = (windowW - rect.width) / 2; // centered; remainder (aspect only) letterboxes
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
    sBaseW = internalW;
    sBaseH = internalH;
    sInternalW = internalW; // render size starts at base (factor 1); resize adjusts
    sInternalH = internalH;

    glGenFramebuffers(1, &sFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sFbo);

    glGenTextures(1, &sColorTex);
    glBindTexture(GL_TEXTURE_2D, sColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, internalW, internalH, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 nullptr);
    // LINEAR sampling: the blit shader does "sharp bilinear" -- nearest across each
    // texel, a sub-pixel bilinear ramp only at texel seams -- so pixels stay crisp
    // at ANY (incl. fractional) scale while the screen fills edge-to-edge. That ramp
    // needs hardware linear filtering to interpolate at the seam.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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
    // Supersample: render at the smallest integer multiple of the base that is >=
    // the window on both axes, so the final blit DOWNSCALES to the window (crisp)
    // instead of upscaling a too-small source (soft). Exact integer windows
    // (1440p=x2, 4K=x3) land render==window -> a 1:1 blit.
    int factor = 1;
    if (sBaseW > 0 && sBaseH > 0)
    {
        const int fx = (windowW + sBaseW - 1) / sBaseW; // ceil(windowW / baseW)
        const int fy = (windowH + sBaseH - 1) / sBaseH;
        factor = std::max(1, std::max(fx, fy));
    }
    const int renderW = sBaseW * factor;
    const int renderH = sBaseH * factor;

    // Reallocate the FBO color texture only when the render size actually changes
    // (resize events can repeat with the same size).
    if (renderW != sInternalW || renderH != sInternalH)
    {
        sInternalW = renderW;
        sInternalH = renderH;
        glBindTexture(GL_TEXTURE_2D, sColorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, renderW, renderH, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

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
    glUniform2f(sLocTexSize, static_cast<float>(sInternalW), static_cast<float>(sInternalH));

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
