#include "Engine.h"

int main(int argc, char* argv[])
{
    Engine engine;

    if (!engine.init("Prison Break", 1280, 720))
        return 1;

    engine.run();
    return 0;
}
