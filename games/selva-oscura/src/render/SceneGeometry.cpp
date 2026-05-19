#include "render/SceneGeometry.h"

#include "render/SceneShaders.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

GLuint sCubeVao = 0;
GLuint sCubeVbo = 0;
GLuint sCubeEbo = 0;

GLuint sGroundVao = 0;
GLuint sGroundVbo = 0;
GLuint sGroundEbo = 0;
int sGroundIndexCount = 0;

GLuint sDiscVao = 0;
GLuint sDiscVbo = 0;
GLuint sDiscEbo = 0;
int sDiscIndexCount = 0;

constexpr float kGroundHalfSize = 500.0f;
constexpr int kGroundSubdiv = 40;

void initCube()
{
    // 8 corners of a unit cube centered at origin, half-extent 0.5. Each
    // vertex is position + grayscale shade. Per-corner shading makes faces
    // gradient between corners so the 3D shape reads when it tumbles —
    // poor-man's lighting until real lighting lands.
    // clang-format off
    static constexpr float kVertices[] = {
        -0.5f, -0.5f, -0.5f,   0.30f,
         0.5f, -0.5f, -0.5f,   0.55f,
         0.5f,  0.5f, -0.5f,   0.80f,
        -0.5f,  0.5f, -0.5f,   0.55f,
        -0.5f, -0.5f,  0.5f,   0.55f,
         0.5f, -0.5f,  0.5f,   0.80f,
         0.5f,  0.5f,  0.5f,   1.00f,
        -0.5f,  0.5f,  0.5f,   0.80f,
    };

    static constexpr unsigned int kIndices[] = {
        0, 2, 1,   0, 3, 2,
        4, 5, 6,   4, 6, 7,
        0, 4, 7,   0, 7, 3,
        1, 2, 6,   1, 6, 5,
        0, 1, 5,   0, 5, 4,
        3, 7, 6,   3, 6, 2,
    };
    // clang-format on

    glGenVertexArrays(1, &sCubeVao);
    glGenBuffers(1, &sCubeVbo);
    glGenBuffers(1, &sCubeEbo);

    glBindVertexArray(sCubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, sCubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sCubeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// Deterministic value-noise sampled on the ground grid. Hash-based so
// adjacent vertices vary independently — produces a stippled register
// that reads as "preserved deadfall / damp earth" once the atmosphere
// shades it, not a flat-color slab.
float hashNoise(int ix, int iz)
{
    uint32_t h = static_cast<uint32_t>(ix * 374761393 + iz * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void initGround()
{
    constexpr int N = kGroundSubdiv;
    constexpr float HS = kGroundHalfSize;
    constexpr int kVertCount = (N + 1) * (N + 1);

    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>(kVertCount * 4));

    // Damp-earth base shade. Per-vertex noise breaks up the flat plane
    // without changing the average tone.
    constexpr float kBaseShade = 0.55f;
    constexpr float kNoiseAmp = 0.08f;

    for (int iz = 0; iz <= N; ++iz)
    {
        for (int ix = 0; ix <= N; ++ix)
        {
            const float u = static_cast<float>(ix) / static_cast<float>(N);
            const float v = static_cast<float>(iz) / static_cast<float>(N);
            const float x = (u * 2.0f - 1.0f) * HS;
            const float z = (v * 2.0f - 1.0f) * HS;
            const float n = (hashNoise(ix, iz) * 2.0f - 1.0f) * kNoiseAmp;
            verts.push_back(x);
            verts.push_back(0.0f);
            verts.push_back(z);
            verts.push_back(kBaseShade + n);
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<std::size_t>(N * N * 6));
    for (int iz = 0; iz < N; ++iz)
    {
        for (int ix = 0; ix < N; ++ix)
        {
            const unsigned int i0 = static_cast<unsigned int>(iz * (N + 1) + ix);
            const unsigned int i1 = i0 + 1;
            const unsigned int i2 = i0 + static_cast<unsigned int>(N + 1);
            const unsigned int i3 = i2 + 1;
            indices.push_back(i0);
            indices.push_back(i3);
            indices.push_back(i1);
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
    sGroundIndexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &sGroundVao);
    glGenBuffers(1, &sGroundVbo);
    glGenBuffers(1, &sGroundEbo);

    glBindVertexArray(sGroundVao);
    glBindBuffer(GL_ARRAY_BUFFER, sGroundVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sGroundEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(),
                 GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void initDisc()
{
    // Flat unit-radius disc on the XZ plane at y=0. Triangle fan from
    // center vertex (index 0) out to a 24-segment rim. Used as a fake
    // contact shadow under trees so the eye anchors them to the floor.
    constexpr int kSegments = 24;
    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>((kSegments + 1) * 4));
    // Center vertex — darkest so the shadow falls off toward the rim.
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    for (int i = 0; i < kSegments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float a = t * 2.0f * 3.14159265f;
        verts.push_back(std::cos(a));
        verts.push_back(0.0f);
        verts.push_back(std::sin(a));
        verts.push_back(1.0f);
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<std::size_t>(kSegments * 3));
    for (int i = 0; i < kSegments; ++i)
    {
        indices.push_back(0);
        indices.push_back(static_cast<unsigned int>(1 + i));
        indices.push_back(static_cast<unsigned int>(1 + ((i + 1) % kSegments)));
    }
    sDiscIndexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &sDiscVao);
    glGenBuffers(1, &sDiscVbo);
    glGenBuffers(1, &sDiscEbo);

    glBindVertexArray(sDiscVao);
    glBindBuffer(GL_ARRAY_BUFFER, sDiscVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sDiscEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(),
                 GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void drawIndexed(GLuint vao, GLsizei index_count, const glm::mat4& model, float tint)
{
    setSceneModel(model);
    setSceneTint(tint);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
}

} // namespace

void initSceneGeometry()
{
    initCube();
    initGround();
    initDisc();
}

void shutdownSceneGeometry()
{
    glDeleteBuffers(1, &sGroundEbo);
    glDeleteBuffers(1, &sGroundVbo);
    glDeleteVertexArrays(1, &sGroundVao);
    glDeleteBuffers(1, &sDiscEbo);
    glDeleteBuffers(1, &sDiscVbo);
    glDeleteVertexArrays(1, &sDiscVao);
    glDeleteBuffers(1, &sCubeEbo);
    glDeleteBuffers(1, &sCubeVbo);
    glDeleteVertexArrays(1, &sCubeVao);
    sGroundEbo = sGroundVbo = sGroundVao = 0;
    sGroundIndexCount = 0;
    sDiscEbo = sDiscVbo = sDiscVao = 0;
    sDiscIndexCount = 0;
    sCubeEbo = sCubeVbo = sCubeVao = 0;
}

void drawGround(const glm::mat4& model, float tint)
{
    drawIndexed(sGroundVao, sGroundIndexCount, model, tint);
}

void drawCube(const glm::mat4& model, float tint)
{
    drawIndexed(sCubeVao, 36, model, tint);
}

void drawDisc(const glm::mat4& model, float tint)
{
    drawIndexed(sDiscVao, sDiscIndexCount, model, tint);
}

} // namespace selva::render
