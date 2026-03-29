#pragma once

class EntityManager;

namespace AIRecorder
{

// Pre-allocate ring buffer. Call once at startup.
void init(int max_entities = 30, float history_seconds = 5.0f);

// Record all AI entities this tick. Call once per fixed-step update.
void tick(EntityManager& em, int frame_number);

// Dump ring buffer to debug/ai_dump_YYYYMMDD_HHMMSS.csv.
void dump();

} // namespace AIRecorder
