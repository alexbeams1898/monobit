// Stub for miniz_export.h. Upstream miniz generates this file via its
// own CMake configure step (CMake's GenerateExportHeader module). For
// the static linkage we use here, no visibility decoration is needed
// — MINIZ_EXPORT can be empty and miniz's symbols resolve via the
// usual static-archive mechanism.
//
// This avoids running miniz's full CMake (which we'd need for nothing
// else, since we only ever pull in miniz for #include compatibility
// with absim_load_file.cpp's unconditional <miniz.h> — we never call
// any zip-extraction functions because we feed plain .hex).
#pragma once
#define MINIZ_EXPORT
