// dump_world — headless build-time binary that runs the same world
// registration code main.cpp does, serializes the structure-footprint
// registry to JSON, and exits. Build-time tools (Python heightmap
// baker, future audio-zone builder, etc) read the generated JSON to
// see the same world state the engine will see at runtime.
//
// Single source of truth: registerAuthoredWorld() is the ONE entry
// that runs in both contexts. Anything authored in there
// (footprints, modifiers, lights, ...) is visible to all consumers.
//
// Run via games/selva-oscura/scripts/bake_terrain.sh; do not invoke
// directly during normal builds — the CMake bake step handles it.

// SDL2 (transitively linked via the engine lib) installs a WinMain
// shim by default on Windows. We're a headless tool; tell it to leave
// main alone. MUST precede any SDL header inclusion that comes in
// through the engine.
#define SDL_MAIN_HANDLED

#include "world/CryptLayout.h"
#include "world/StructureFootprints.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
void printUsage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s --out <path-to-output.json>\n"
                 "\n"
                 "Runs selva::world::crypt_layout::registerAuthoredWorld() and\n"
                 "writes the resulting StructureFootprint registry to the given\n"
                 "JSON file. Build-time tools consume the file to mirror the\n"
                 "runtime registry without linking the engine.\n",
                 argv0);
}
} // namespace

int main(int argc, char** argv)
{
    const char* out_path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            std::fprintf(stderr, "[dump_world] unknown arg: %s\n", argv[i]);
            printUsage(argv[0]);
            return 2;
        }
    }
    if (out_path == nullptr)
    {
        printUsage(argv[0]);
        return 2;
    }

    selva::world::crypt_layout::registerAuthoredWorld();

    if (!engine::world::serializeStructureRegistryJson(out_path))
    {
        std::fprintf(stderr, "[dump_world] serialize failed: %s\n", out_path);
        return 1;
    }
    std::fprintf(stderr, "[dump_world] wrote %s (%d structures)\n", out_path,
                 engine::world::structureFootprintCount());
    return 0;
}
