#include "systems/RenderSystem.h"

#include "ecs/Components.h"

#include <SDL_opengl.h>

void RenderSystem::init(int windowW, int windowH)
{
    // Set viewport to cover the full window.
    glViewport(0, 0, windowW, windowH);

    // Orthographic projection: (0,0) = top-left, (windowW, windowH) = bottom-right.
    // This matches SDL's pixel coordinate convention so positions in world space
    // map directly to screen pixels.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(windowW), static_cast<double>(windowH), 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void RenderSystem::render(EntityManager& em)
{
    // Teal blob color — visible against the dark grey background.
    glColor3f(0.2f, 0.8f, 0.7f);

    // Draw every entity that has a world position and a known size.
    // Collider width/height is reused as the visual footprint — Sprite.srcW/srcH
    // will replace this in Issue #6 once textures are real.
    for (auto [entity, transform, collider] : em.registry().view<Transform, Collider>().each())
    {
        // Transform stores the entity centre; draw from the top-left corner.
        const float x = transform.x - collider.width * 0.5f;
        const float y = transform.y - collider.height * 0.5f;
        const float w = collider.width;
        const float h = collider.height;

        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y + h);
        glVertex2f(x, y + h);
        glEnd();
    }
}
