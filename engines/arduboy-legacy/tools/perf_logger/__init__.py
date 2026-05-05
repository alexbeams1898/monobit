"""Perf logger — analyze binary frame traces from the SDL build.

Capture a trace:
    make sdl-build-log
    ./build-sdl/bin/mono-sdl.exe > trace.bin
    # play for a while, close window

Analyze:
    python -m tools.perf_logger trace.bin --summary
    python -m tools.perf_logger trace.bin --timeline --filter MAIN_MENU
    python -m tools.perf_logger trace.bin --top 20

All perf tooling lives under this directory — source-of-truth for AVR
cycle costs (avr_costs.json), the analyzer, the state-name parser, and
the binary format definition.
"""
