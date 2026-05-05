#include "Engine.h"

#include <cstdio>

int main(int /*argc*/, char* /*argv*/[])
{
    Engine engine;

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    engine.setClearColor(0.0f, 0.0f, 0.0f);

    engine.run();
    engine.shutdown();
    return 0;
}
