#pragma once

#include <cstdint>
#include <string>

// Lightweight, always-on diagnostics for early Switch hardware testing. All
// calls are made by the main thread; the API deliberately avoids allocations
// at checkpoint sites so it can also identify allocation-related stalls.
void switchDebugFrameBegin(bool hasWorld, bool hasPlayer);
void switchDebugCheckpoint(const char *stage);
void switchDebugWorldRenderComplete(std::uint64_t elapsedMicros);
void switchDebugTickComplete();
void switchDebugTerrainListsRequested(int count);
void switchDebugDisplayListResult(bool found, bool drawn, int vertices);
std::string switchDebugLine(int line);

// A world query was skipped because its box was non-finite or oversized; the
// entity that issued it is reported in status.log.
void switchDebugBadBox(const char *query, const char *entityType, double x, double y, double z);
// Player state text, appended to sdmc:/switch/OptiCraft/status.log every
// few seconds.
void switchDebugPlayerStatus(const char *text);
// Appends one diagnostic line to status.log (shares its line cap).
void switchDebugNote(const char *text);
// While a system applet (software keyboard) owns the screen the main thread
// legitimately starts no frames; the stall watchdog ignores that time.
void switchDebugSuspendWatchdog(bool suspended);

// Frame-time breakdown. Durations are in system ticks (armGetSystemTick);
// every ~300 frames the per-frame averages go to status.log as one "perf"
// line, so a slow frame can be attributed to a phase without a debugger.
void switchProfileRenderPhase(int phase, std::uint64_t ticks);
void switchProfileSwap(std::uint64_t ticks);
void switchProfileTickPhase(const char *name, long long nanoseconds);
// Lighting jobs still queued after this frame's drain (reported as lightQ).
void switchProfileLightQueue(int pending);
