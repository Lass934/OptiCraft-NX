#include "switch/SwitchRuntimeDebug.h"

#include "platform/Diagnostics.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <switch.h>

namespace
{
std::atomic<const char *> g_stage{"boot"};
std::atomic<std::uint64_t> g_frame{0};
std::atomic<std::uint64_t> g_ticks{0};
std::uint64_t g_lastWorldRenderMicros = 0;
std::atomic<bool> g_hasWorld{false};
bool g_hasPlayer = false;
std::uint64_t g_terrainListsRequested = 0;
std::uint64_t g_displayListsFound = 0;
std::uint64_t g_displayListsMissing = 0;
std::uint64_t g_drawCalls = 0;
std::uint64_t g_vertices = 0;

// Stall watchdog.
//
// The SWDBG overlay is drawn by the HUD, so a frozen screen always shows the
// stage the HUD ran in ("hand") rather than where the main thread stopped.
// Once a world is open, this thread checks that frames keep starting. After
// kStallPolls polls without one it briefly pauses the main thread, copies its
// registers and stack, resumes it, and appends PC/LR plus every stack word that
// points into our code segment to sdmc:/switch/OptiCraft/stall.log. Offsets are
// relative to the module base, so `aarch64-none-elf-addr2line -fCe
// OptiCraft.elf <offset>` on the matching build turns them into a call stack.
//
// It runs at a higher priority than the main thread (0x2B vs 0x2C) so a main
// thread spinning on its core cannot starve it, and it never takes a lock or
// allocates while the main thread is paused. File output uses open()/write()
// with stack buffers rather than stdio, which would malloc a FILE.
constexpr u64 kPollNs = 500000000ULL;
constexpr int kStallPolls = 6; // 3 s without a new frame
constexpr int kSamples = 3;    // spaced one second apart: spinning vs. waiting
constexpr std::size_t kStackCopyBytes = 64 * 1024;
constexpr int kMaxStackHits = 48;

Thread *g_mainThread = nullptr;
Thread g_watchThread;
std::atomic<bool> g_watchStarted{false};
std::atomic<bool> g_watchSuspended{false};
u64 g_textBase = 0;
u64 g_textSize = 0;
alignas(16) u64 g_stackCopy[kStackCopyBytes / sizeof(u64)];

bool inText(u64 address)
{
    return address >= g_textBase && address < g_textBase + g_textSize;
}

unsigned long long textOffset(u64 address)
{
    return static_cast<unsigned long long>(inText(address) ? address - g_textBase : address);
}

__attribute__((format(printf, 4, 5)))
void appendText(char *buffer, std::size_t capacity, std::size_t &length, const char *format, ...)
{
    if (length + 1 >= capacity)
        return;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer + length, capacity - length, format, args);
    va_end(args);
    if (written <= 0)
        return;
    const std::size_t room = capacity - length - 1;
    length += static_cast<std::size_t>(written) < room ? static_cast<std::size_t>(written) : room;
}

void writeLog(const char *text, std::size_t length)
{
    const int fd = open("sdmc:/switch/OptiCraft/stall.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    while (length > 0)
    {
        const ssize_t written = write(fd, text, length);
        if (written <= 0)
            break;
        text += written;
        length -= static_cast<std::size_t>(written);
    }
    close(fd);
}

void sampleMainThread(int sample, std::uint64_t frame)
{
    char text[4096];
    std::size_t length = 0;
    appendText(text, sizeof(text), length, "-- sample %d frame=%llu ticks=%llu stage=%s\n", sample,
        static_cast<unsigned long long>(frame),
        static_cast<unsigned long long>(g_ticks.load(std::memory_order_relaxed)),
        g_stage.load(std::memory_order_relaxed));

    ThreadContext context;
    std::memset(&context, 0, sizeof(context));
    std::size_t copied = 0;
    if (R_FAILED(threadPause(g_mainThread)))
    {
        appendText(text, sizeof(text), length, "threadPause failed\n");
        writeLog(text, length);
        return;
    }
    // Nothing between pause and resume may lock or allocate: the main thread
    // could be holding the allocator or stdio lock right now.
    const Result dumped = threadDumpContext(&context, g_mainThread);
    if (R_SUCCEEDED(dumped))
    {
        MemoryInfo stackInfo{};
        u32 pageInfo = 0;
        if (R_SUCCEEDED(svcQueryMemory(&stackInfo, &pageInfo, context.sp)) &&
            context.sp >= stackInfo.addr && context.sp < stackInfo.addr + stackInfo.size)
        {
            copied = static_cast<std::size_t>(stackInfo.addr + stackInfo.size - context.sp);
            if (copied > kStackCopyBytes)
                copied = kStackCopyBytes;
            std::memcpy(g_stackCopy, reinterpret_cast<const void *>(context.sp), copied);
        }
    }
    threadResume(g_mainThread);

    if (R_FAILED(dumped))
    {
        appendText(text, sizeof(text), length, "threadDumpContext failed 0x%x\n", static_cast<unsigned>(dumped));
        writeLog(text, length);
        return;
    }

    appendText(text, sizeof(text), length, "pc=0x%llx%s lr=0x%llx%s sp=0x%llx\n",
        textOffset(context.pc.x), inText(context.pc.x) ? "" : "(abs)",
        textOffset(context.lr), inText(context.lr) ? "" : "(abs)",
        static_cast<unsigned long long>(context.sp));
    appendText(text, sizeof(text), length, "stack:");
    int hits = 0;
    for (std::size_t i = 0; i < copied / sizeof(u64) && hits < kMaxStackHits; ++i)
    {
        if (!inText(g_stackCopy[i]))
            continue;
        appendText(text, sizeof(text), length, " 0x%llx", textOffset(g_stackCopy[i]));
        ++hits;
    }
    appendText(text, sizeof(text), length, "\n");
    writeLog(text, length);
}

void watchdogMain(void *)
{
    std::uint64_t lastFrame = g_frame.load(std::memory_order_relaxed);
    int stillPolls = 0;
    bool reported = false;
    for (;;)
    {
        svcSleepThread(kPollNs);
        const std::uint64_t frame = g_frame.load(std::memory_order_relaxed);
        if (frame != lastFrame || g_watchSuspended.load(std::memory_order_relaxed))
        {
            lastFrame = frame;
            stillPolls = 0;
            reported = false;
            continue;
        }
        if (reported || !g_hasWorld.load(std::memory_order_relaxed) || ++stillPolls < kStallPolls)
            continue;

        reported = true;
        char header[192];
        std::size_t length = 0;
        appendText(header, sizeof(header), length,
            "=== STALL: no frame for %d ms, text base=0x%llx size=0x%llx ===\n",
            kStallPolls * static_cast<int>(kPollNs / 1000000ULL),
            static_cast<unsigned long long>(g_textBase),
            static_cast<unsigned long long>(g_textSize));
        writeLog(header, length);
        for (int sample = 0; sample < kSamples; ++sample)
        {
            if (sample > 0)
                svcSleepThread(kPollNs * 2);
            if (g_frame.load(std::memory_order_relaxed) != frame)
                break;
            sampleMainThread(sample, frame);
        }
    }
}

void startWatchdog()
{
    if (g_watchStarted.exchange(true))
        return;
    g_mainThread = threadGetSelf();
    MemoryInfo textInfo{};
    u32 pageInfo = 0;
    if (R_FAILED(svcQueryMemory(&textInfo, &pageInfo, reinterpret_cast<u64>(&startWatchdog))))
        return;
    g_textBase = textInfo.addr;
    g_textSize = textInfo.size;
    if (R_FAILED(threadCreate(&g_watchThread, watchdogMain, nullptr, nullptr, 0x8000, 0x2B, -2)))
        return;
    threadStart(&g_watchThread);
}
}

namespace
{
// Indexed by PlatformRenderPhase (Sky..HudHints = 0..12).
constexpr int kProfilePhases = 13;
constexpr const char *kProfilePhaseNames[kProfilePhases] = {
    "sky", "frustum", "build", "opaque", "entities", "translucent", "hand", "hud",
    "entDraw", "teDraw", "hudItems", "hudText", "hudHints"};
constexpr std::uint64_t kProfileFrames = 300;

std::uint64_t g_profPhase[kProfilePhases] = {};
std::uint64_t g_profSwap = 0;
std::uint64_t g_profTick = 0;
std::uint64_t g_profTicksRun = 0;
int g_profLightQueue = 0;
int g_profLightQueueMax = 0;
// Named tick sub-phases (ClientProfiler::tickPhase / platformProfileTickPhase)
// plus per-frame lighting, accumulated by name. The names are string literals
// from a fixed set, so a small linear table is enough.
struct NamedPhase
{
    const char *name = nullptr;
    std::uint64_t ns = 0;
    std::uint64_t peakNs = 0; // longest single sample in the window
};
constexpr int kNamedPhases = 32;
NamedPhase g_profNamed[kNamedPhases];

// Wall time between consecutive debug checkpoints, charged to the stage that
// was active. The checkpoints already bracket the whole frame (tick stages,
// world render stages, HUD after "world-done", present), so this covers the
// frame without gaps -- including the parts no render phase measures.
struct StageTime
{
    const char *name = nullptr;
    std::uint64_t ticks = 0;
    std::uint64_t peakTicks = 0;
};
constexpr int kStages = 48;
StageTime g_profStages[kStages];
const char *g_stageName = nullptr;
std::uint64_t g_stageStart = 0;

void chargeStage(const char *name, std::uint64_t ticks)
{
    if (name == nullptr)
        return;
    for (StageTime &stage : g_profStages)
    {
        if (stage.name == nullptr)
            stage.name = name;
        if (stage.name == name || std::strcmp(stage.name, name) == 0)
        {
            stage.ticks += ticks;
            if (ticks > stage.peakTicks)
                stage.peakTicks = ticks;
            return;
        }
    }
}

void enterStage(const char *name)
{
    const std::uint64_t now = armGetSystemTick();
    if (g_stageStart != 0)
        chargeStage(g_stageName, now - g_stageStart);
    g_stageName = name;
    g_stageStart = now;
    g_stage.store(name, std::memory_order_relaxed);
}
std::uint64_t g_profFrames = 0;
std::uint64_t g_profWindowStart = 0;
std::uint64_t g_profTickStart = 0;

double ticksToMs(std::uint64_t ticks)
{
    return static_cast<double>(armTicksToNs(ticks)) / 1.0e6;
}

void profileFrameBoundary()
{
    const std::uint64_t now = armGetSystemTick();
    if (g_profWindowStart == 0)
    {
        g_profWindowStart = now;
        return;
    }
    if (++g_profFrames < kProfileFrames)
        return;

    const double frames = static_cast<double>(g_profFrames);
    char text[600];
    int length = std::snprintf(text, sizeof(text), "perf frame=%.1fms swap=%.1f tick=%.1f(x%.2f) lightQ=%d(max %d)",
        ticksToMs(now - g_profWindowStart) / frames, ticksToMs(g_profSwap) / frames,
        ticksToMs(g_profTick) / frames, static_cast<double>(g_profTicksRun) / frames,
        g_profLightQueue, g_profLightQueueMax);
    g_profLightQueueMax = 0;
    for (int i = 0; i < kProfilePhases && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
        length += std::snprintf(text + length, sizeof(text) - length, " %s=%.1f",
            kProfilePhaseNames[i], ticksToMs(g_profPhase[i]) / frames);
    switchDebugNote(text);

    // Second line: every named sub-phase costing at least 0.1 ms per frame,
    // most expensive first (ms per rendered frame, like the line above).
    NamedPhase sorted[kNamedPhases];
    int count = 0;
    for (const NamedPhase &phase : g_profNamed)
        if (phase.name != nullptr)
            sorted[count++] = phase;
    std::sort(sorted, sorted + count, [](const NamedPhase &a, const NamedPhase &b) { return a.ns > b.ns; });
    length = std::snprintf(text, sizeof(text), "perf phases:");
    for (int i = 0; i < count && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
    {
        const double ms = static_cast<double>(sorted[i].ns) / 1.0e6 / frames;
        if (ms < 0.1)
            break;
        length += std::snprintf(text + length, sizeof(text) - length, " %s=%.1f", sorted[i].name, ms);
    }
    switchDebugNote(text);

    StageTime stages[kStages];
    int stageCount = 0;
    for (const StageTime &stage : g_profStages)
        if (stage.name != nullptr)
            stages[stageCount++] = stage;
    std::sort(stages, stages + stageCount, [](const StageTime &a, const StageTime &b) { return a.ticks > b.ticks; });
    length = std::snprintf(text, sizeof(text), "perf stages:");
    for (int i = 0; i < stageCount && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
    {
        const double ms = ticksToMs(stages[i].ticks) / frames;
        if (ms < 0.1)
            break;
        length += std::snprintf(text + length, sizeof(text) - length, " %s=%.1f", stages[i].name, ms);
    }
    switchDebugNote(text);

    // Third line: single samples of 1.5 ms or more. The averages above hide a
    // phase that is cheap on most frames and stalls one of them.
    struct Peak
    {
        const char *name;
        double ms;
    };
    Peak peaks[kNamedPhases + kStages];
    int peakCount = 0;
    for (const NamedPhase &phase : g_profNamed)
        if (phase.name != nullptr && static_cast<double>(phase.peakNs) / 1.0e6 >= 1.5)
            peaks[peakCount++] = {phase.name, static_cast<double>(phase.peakNs) / 1.0e6};
    for (const StageTime &stage : g_profStages)
        if (stage.name != nullptr && ticksToMs(stage.peakTicks) >= 1.5)
            peaks[peakCount++] = {stage.name, ticksToMs(stage.peakTicks)};
    std::sort(peaks, peaks + peakCount, [](const Peak &a, const Peak &b) { return a.ms > b.ms; });
    length = std::snprintf(text, sizeof(text), "perf peaks:");
    for (int i = 0; i < peakCount && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
        length += std::snprintf(text + length, sizeof(text) - length, " %s=%.1f", peaks[i].name, peaks[i].ms);
    switchDebugNote(text);

    for (StageTime &stage : g_profStages)
        stage.ticks = stage.peakTicks = 0;

    std::memset(g_profPhase, 0, sizeof(g_profPhase));
    for (NamedPhase &phase : g_profNamed)
        phase.ns = phase.peakNs = 0;
    g_profSwap = g_profTick = g_profTicksRun = 0;
    g_profFrames = 0;
    g_profWindowStart = now;
}
}

void switchProfileRenderPhase(int phase, std::uint64_t ticks)
{
    if (phase >= 0 && phase < kProfilePhases)
        g_profPhase[phase] += ticks;
}

void switchProfileSwap(std::uint64_t ticks)
{
    g_profSwap += ticks;
}

void switchProfileTickPhase(const char *name, long long nanoseconds)
{
    if (name == nullptr || nanoseconds <= 0)
        return;
    for (NamedPhase &phase : g_profNamed)
    {
        if (phase.name == nullptr)
            phase.name = name; // first free slot: register this name
        if (phase.name == name || std::strcmp(phase.name, name) == 0)
        {
            phase.ns += static_cast<std::uint64_t>(nanoseconds);
            if (static_cast<std::uint64_t>(nanoseconds) > phase.peakNs)
                phase.peakNs = static_cast<std::uint64_t>(nanoseconds);
            return;
        }
    }
}

void switchDebugFrameBegin(bool hasWorld, bool hasPlayer)
{
    if (hasWorld)
    {
        startWatchdog();
        profileFrameBoundary();
    }
    g_frame.fetch_add(1, std::memory_order_relaxed);
    g_hasWorld.store(hasWorld, std::memory_order_relaxed);
    g_hasPlayer = hasPlayer;
    g_terrainListsRequested = 0;
    g_displayListsFound = 0;
    g_displayListsMissing = 0;
    g_drawCalls = 0;
    g_vertices = 0;
    enterStage("frame-begin");
}

void switchDebugTerrainListsRequested(int count)
{
    if (count > 0)
        g_terrainListsRequested += static_cast<std::uint64_t>(count);
}

void switchDebugDisplayListResult(bool found, bool drawn, int vertices)
{
    if (!found)
    {
        ++g_displayListsMissing;
        return;
    }
    ++g_displayListsFound;
    if (drawn)
    {
        ++g_drawCalls;
        if (vertices > 0)
            g_vertices += static_cast<std::uint64_t>(vertices);
    }
}

void switchDebugCheckpoint(const char *stage)
{
    if (stage != nullptr && std::strcmp(stage, "tick-begin") == 0)
        g_profTickStart = armGetSystemTick();
    enterStage(stage ? stage : "(null)");
}

void switchDebugWorldRenderComplete(std::uint64_t elapsedMicros)
{
    g_lastWorldRenderMicros = elapsedMicros;
    enterStage("world-done");
}

void switchDebugTickComplete()
{
    if (g_profTickStart != 0)
    {
        g_profTick += armGetSystemTick() - g_profTickStart;
        ++g_profTicksRun;
        g_profTickStart = 0;
    }
    g_ticks.fetch_add(1, std::memory_order_relaxed);
    enterStage("tick-done");
}

namespace
{
char g_badBoxText[160] = "badbox none";
std::uint64_t g_badBoxCount = 0;
char g_playerText[160] = "player -";
std::uint64_t g_lastStatusFrame = 0;
int g_statusLinesWritten = 0;
constexpr std::uint64_t kStatusEveryFrames = 150;
constexpr int kMaxStatusLines = 400;

void writeStatus(const char *text)
{
    if (g_statusLinesWritten >= kMaxStatusLines)
        return;
    ++g_statusLinesWritten;
    const int fd = open("sdmc:/switch/OptiCraft/status.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    char line[400];
    const int length = std::snprintf(line, sizeof(line), "f=%llu t=%llu %s\n",
        static_cast<unsigned long long>(g_frame.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(g_ticks.load(std::memory_order_relaxed)), text);
    if (length > 0)
        write(fd, line, static_cast<std::size_t>(length) < sizeof(line) ? static_cast<std::size_t>(length) : sizeof(line) - 1);
    close(fd);
}
}

void switchDebugBadBox(const char *query, const char *entityType, double x, double y, double z)
{
    ++g_badBoxCount;
    std::snprintf(g_badBoxText, sizeof(g_badBoxText), "badbox n=%llu %s %s pos=%.6g,%.6g,%.6g",
        static_cast<unsigned long long>(g_badBoxCount), query ? query : "?",
        entityType ? entityType : "?", x, y, z);
    // The first few occurrences are enough to identify the entity.
    if (g_badBoxCount <= 5)
        writeStatus(g_badBoxText);
}

void switchDebugSuspendWatchdog(bool suspended)
{
    g_watchSuspended.store(suspended, std::memory_order_relaxed);
}

void switchDebugNote(const char *text)
{
    writeStatus(text ? text : "-");
}

void switchDebugPlayerStatus(const char *text)
{
    std::snprintf(g_playerText, sizeof(g_playerText), "%s", text ? text : "-");
    const std::uint64_t frame = g_frame.load(std::memory_order_relaxed);
    if (frame - g_lastStatusFrame >= kStatusEveryFrames)
    {
        g_lastStatusFrame = frame;
        writeStatus(g_playerText);
        writeStatus(switchDebugLine(2).c_str());
    }
}

std::string switchDebugLine(int line)
{
    if (line == 3)
        return g_badBoxText;
    if (line == 4)
        return g_playerText;

    char text[160]{};
    char heapText[32]{};
    const long heapKb = platformHeapFreeKb();
    if (heapKb < 0)
        std::snprintf(heapText, sizeof(heapText), "n/a");
    else
        std::snprintf(heapText, sizeof(heapText), "%ld KB", heapKb);
    switch (line)
    {
        case 0:
            std::snprintf(text, sizeof(text), "SWDBG f=%llu t=%llu stage=%s",
                static_cast<unsigned long long>(g_frame.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(g_ticks.load(std::memory_order_relaxed)),
                g_stage.load(std::memory_order_relaxed));
            break;
        case 1:
            std::snprintf(text, sizeof(text), "world=%d player=%d render=%llu us heap=%s",
                g_hasWorld.load(std::memory_order_relaxed) ? 1 : 0, g_hasPlayer ? 1 : 0,
                static_cast<unsigned long long>(g_lastWorldRenderMicros), heapText);
            break;
        case 2:
            std::snprintf(text, sizeof(text), "terrain=%llu lists=%llu missing=%llu draws=%llu verts=%llu",
                static_cast<unsigned long long>(g_terrainListsRequested),
                static_cast<unsigned long long>(g_displayListsFound),
                static_cast<unsigned long long>(g_displayListsMissing),
                static_cast<unsigned long long>(g_drawCalls),
                static_cast<unsigned long long>(g_vertices));
            break;
        default:
            return {};
    }
    return text;
}

void switchProfileLightQueue(int pending)
{
    g_profLightQueue = pending;
    if (pending > g_profLightQueueMax)
        g_profLightQueueMax = pending;
}
