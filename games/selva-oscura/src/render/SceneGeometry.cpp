#include "render/SceneGeometry.h"

#include <glad/glad.h>
#include <vector>

#include "render/SceneShaders.h"

namespace selva::render
{

namespace
{

GLuint sCubeVao = 0;
GLuint sCubeVbo = 0;
GLuint sCubeEbo = 0;

GLuint sFloorVao = 0;
GLuint sFloorVbo = 0;
GLuint sFloorEbo = 0;

GLuint sGridVao = 0;
GLuint sGridVbo = 0;
int sGridLineCount = 0;
GLuint sAxesVao = 0;
GLuint sAxesVbo = 0;
int sAxesLineCount = 0;

constexpr float kFloorHalfSize = 50.0f;

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

void initFloor()
{
    // clang-format off
    const float kVertices[] = {
        -kFloorHalfSize, 0.0f, -kFloorHalfSize,   0.10f,
         kFloorHalfSize, 0.0f, -kFloorHalfSize,   0.10f,
         kFloorHalfSize, 0.0f,  kFloorHalfSize,   0.25f,
        -kFloorHalfSize, 0.0f,  kFloorHalfSize,   0.25f,
    };

    static constexpr unsigned int kIndices[] = {
        0, 2, 1,   0, 3, 2,
    };
    // clang-format on

    glGenVertexArrays(1, &sFloorVao);
    glGenBuffers(1, &sFloorVbo);
    glGenBuffers(1, &sFloorEbo);

    glBindVertexArray(sFloorVao);
    glBindBuffer(GL_ARRAY_BUFFER, sFloorVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sFloorEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void initGrid()
{
    constexpr int kGridHalfSize = 20;
    const float k = static_cast<float>(kGridHalfSize);
    std::vector<float> verts;
    verts.reserve(static_cast<std::size_t>((kGridHalfSize * 2 + 1) * 4 * 4));
    for (int i = -kGridHalfSize; i <= kGridHalfSize; ++i)
    {
        if (i == 0)
            continue;
        const float p = static_cast<float>(i);
        verts.push_back(-k); verts.push_back(0.005f); verts.push_back(p); verts.push_back(0.20f);
        verts.push_back(k);  verts.push_back(0.005f); verts.push_back(p); verts.push_back(0.20f);
        verts.push_back(p);  verts.push_back(0.005f); verts.push_back(-k); verts.push_back(0.20f);
        verts.push_back(p);  verts.push_back(0.005f); verts.push_back(k);  verts.push_back(0.20f);
    }
    sGridLineCount = static_cast<int>(verts.size() / 4);

    glGenVertexArrays(1, &sGridVao);
    glGenBuffers(1, &sGridVbo);
    glBindVertexArray(sGridVao);
    glBindBuffer(GL_ARRAY_BUFFER, sGridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    constexpr int stride = 4 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    const float kAxes[] = {
        -k,   0.006f, 0.0f, 0.55f,
         k,   0.006f, 0.0f, 0.55f,
         0.0f, 0.006f, -k,  0.75f,
         0.0f, 0.006f,  k,  0.75f,
    };
    sAxesLineCount = 4;
    glGenVertexArrays(1, &sAxesVao);
    glGenBuffers(1, &sAxesVbo);
    glBindVertexArray(sAxesVao);
    glBindBuffer(GL_ARRAY_BUFFER, sAxesVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kAxes), kAxes, GL_STATIC_DRAW);
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

void drawLineArray(GLuint vao, GLsizei vertex_count, const glm::mat4& model, float tint)
{
    setSceneModel(model);
    setSceneTint(tint);
    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, vertex_count);
}

} // namespace

void initSceneGeometry()
{
    initCube();
    initFloor();
    initGrid();
}

void shutdownSceneGeometry()
{
    glDeleteBuffers(1, &sFloorEbo);
    glDeleteBuffers(1, &sFloorVbo);
    glDeleteVertexArrays(1, &sFloorVao);
    glDeleteBuffers(1, &sGridVbo);
    glDeleteVertexArrays(1, &sGridVao);
    glDeleteBuffers(1, &sAxesVbo);
    glDeleteVertexArrays(1, &sAxesVao);
    glDeleteBuffers(1, &sCubeEbo);
    glDeleteBuffers(1, &sCubeVbo);
    glDeleteVertexArrays(1, &sCubeVao);
    sFloorEbo = sFloorVbo = sFloorVao = 0;
    sGridVbo = sGridVao = 0;
    sAxesVbo = sAxesVao = 0;
    sCubeEbo = sCubeVbo = sCubeVao = 0;
    sGridLineCount = 0;
    sAxesLineCount = 0;
}

void drawFloor(const glm::mat4& model, float tint)
{
    drawIndexed(sFloorVao, 6, model, tint);
}

void drawCube(const glm::mat4& model, float tint)
{
    drawIndexed(sCubeVao, 36, model, tint);
}

void drawGrid(const glm::mat4& model, float tint)
{
    drawLineArray(sGridVao, sGridLineCount, model, tint);
}

void drawAxes(const glm::mat4& model, float tint)
{
    drawLineArray(sAxesVao, sAxesLineCount, model, tint);
}

} // namespace selva::render
