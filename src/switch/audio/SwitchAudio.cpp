#include "switch/audio/SwitchAudio.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "pc/external/stb_vorbis.h"
#include "platform/audio/VorbisAssetOpen.h"

namespace SwitchAudio
{
namespace
{
constexpr int kRate = 48000;
constexpr int kFrames = 1024;          // per output buffer: ~21 ms
constexpr int kBuffers = 4;            // ~85 ms queued
constexpr int kMaxVoices = 32;
constexpr std::size_t kCacheBytes = 24u * 1024u * 1024u;
constexpr std::size_t kBufferBytes = kFrames * 2 * sizeof(std::int16_t);
constexpr std::size_t kBufferAlloc = (kBufferBytes + 0xFFF) & ~static_cast<std::size_t>(0xFFF);

// Decoded sound, at its own rate and channel count.
struct Pcm
{
    std::vector<std::int16_t> samples;
    int channels = 1;
    int rate = kRate;
    std::size_t frames() const { return samples.size() / static_cast<std::size_t>(channels); }
};
using PcmRef = std::shared_ptr<const Pcm>;

struct Voice
{
    PcmRef pcm;
    double position = 0.0;  // in source frames
    double step = 1.0;      // source frames per output frame
    float gainLeft = 1.0f;
    float gainRight = 1.0f;
};

// A file decoded as it plays. Opened by the loader thread, then owned by the
// mixer thread.
struct StreamState
{
    stb_vorbis *vorbis = nullptr;
    int channels = 2;
    double step = 1.0;
    float volume = 1.0f;
    std::vector<std::int16_t> chunk;
    std::size_t chunkFrames = 0;
    std::size_t chunkPos = 0;
    float previous[2] = {0.0f, 0.0f};
    float current[2] = {0.0f, 0.0f};
    double fraction = 0.0;
    bool primed = false;
    std::uint32_t generation = 0;

    ~StreamState()
    {
        if (vorbis != nullptr)
            stb_vorbis_close(vorbis);
    }

    // Next source frame into current[], false at the end of the file.
    bool advance()
    {
        if (chunkPos >= chunkFrames)
        {
            chunk.resize(static_cast<std::size_t>(1024 * channels));
            const int got = stb_vorbis_get_samples_short_interleaved(
                vorbis, channels, chunk.data(), static_cast<int>(chunk.size()));
            if (got <= 0)
                return false;
            chunkFrames = static_cast<std::size_t>(got);
            chunkPos = 0;
        }
        const std::int16_t *frame = chunk.data() + chunkPos * static_cast<std::size_t>(channels);
        previous[0] = current[0];
        previous[1] = current[1];
        current[0] = frame[0];
        current[1] = channels > 1 ? frame[1] : frame[0];
        ++chunkPos;
        return true;
    }
};

struct Request
{
    enum class Kind
    {
        Sound,
        Stream
    } kind = Kind::Sound;
    std::string path;
    float volume = 1.0f;
    float pitch = 1.0f;
    float gainLeft = 1.0f;
    float gainRight = 1.0f;
    Stream stream = Stream::Music;
    std::uint32_t generation = 0; // stream requests: dropped if stopped meanwhile
};

struct State
{
    // Shared between threads, under mutex.
    std::mutex mutex;
    std::condition_variable loaderWake;
    std::deque<Request> requests;
    std::vector<Voice> newVoices;
    std::unique_ptr<StreamState> newStreams[2];
    bool stopStreams[2] = {false, false};
    bool quit = false;

    std::atomic<float> soundVolume{1.0f};
    std::atomic<float> musicVolume{1.0f};
    std::atomic<bool> streamActive[2] = {{false}, {false}};
    std::uint32_t streamGeneration[2] = {0, 0}; // game thread + loader, under mutex

    float listener[3] = {0.0f, 0.0f, 0.0f};
    float yawRadians = 0.0f;

    bool running = false;
    Thread mixerThread{};
    Thread loaderThread{};
    std::int16_t *buffers[kBuffers] = {};
    AudioOutBuffer outBuffers[kBuffers] = {};
};

State g;

int streamIndex(Stream stream) { return stream == Stream::Music ? 0 : 1; }

// ---------------------------------------------------------------------------
// Loader thread: decodes sounds (cached) and opens streams.
// ---------------------------------------------------------------------------
std::unordered_map<std::string, PcmRef> g_cache; // loader thread only
std::size_t g_cacheBytes = 0;

PcmRef loadSound(const std::string &path)
{
    const auto found = g_cache.find(path);
    if (found != g_cache.end())
        return found->second;

    int channels = 0;
    int rate = 0;
    short *data = nullptr;
    const int frames = platformDecodeVorbis(path, &channels, &rate, &data);
    if (frames <= 0 || data == nullptr || channels <= 0 || rate <= 0)
    {
        g_cache.emplace(path, nullptr); // don't retry a broken file every time
        return nullptr;
    }
    auto pcm = std::make_shared<Pcm>();
    pcm->channels = std::min(channels, 2);
    pcm->rate = rate;
    pcm->samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(pcm->channels));
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < pcm->channels; ++c)
            pcm->samples[static_cast<std::size_t>(i * pcm->channels + c)] = data[i * channels + c];
    std::free(data);

    const std::size_t bytes = pcm->samples.size() * sizeof(std::int16_t);
    // Evict sounds nobody is playing until the new one fits.
    for (auto it = g_cache.begin(); g_cacheBytes + bytes > kCacheBytes && it != g_cache.end();)
    {
        if (it->second && it->second.use_count() == 1)
        {
            g_cacheBytes -= it->second->samples.size() * sizeof(std::int16_t);
            it = g_cache.erase(it);
        }
        else
            ++it;
    }
    g_cacheBytes += bytes;
    g_cache.emplace(path, pcm);
    return pcm;
}

void loaderMain(void *)
{
    for (;;)
    {
        Request request;
        {
            std::unique_lock<std::mutex> lock(g.mutex);
            g.loaderWake.wait(lock, [] { return g.quit || !g.requests.empty(); });
            if (g.quit)
                return;
            request = std::move(g.requests.front());
            g.requests.pop_front();
        }

        if (request.kind == Request::Kind::Sound)
        {
            PcmRef pcm = loadSound(request.path);
            if (!pcm)
                continue;
            Voice voice;
            voice.pcm = pcm;
            voice.step = static_cast<double>(pcm->rate) / kRate * std::max(0.1f, request.pitch);
            voice.gainLeft = request.volume * request.gainLeft;
            voice.gainRight = request.volume * request.gainRight;
            std::lock_guard<std::mutex> lock(g.mutex);
            if (g.newVoices.size() < kMaxVoices)
                g.newVoices.push_back(std::move(voice));
            continue;
        }

        // Stream: open here, the mixer only decodes.
        const int index = streamIndex(request.stream);
        int error = 0;
        auto stream = std::make_unique<StreamState>();
        stream->vorbis = platformOpenVorbis(request.path, &error);
        std::lock_guard<std::mutex> lock(g.mutex);
        if (request.generation != g.streamGeneration[index])
            continue; // stopped or replaced while opening
        if (stream->vorbis == nullptr)
        {
            g.streamActive[index].store(false);
            continue;
        }
        const stb_vorbis_info info = stb_vorbis_get_info(stream->vorbis);
        stream->channels = std::max(1, info.channels);
        stream->step = static_cast<double>(info.sample_rate) / kRate;
        stream->volume = request.volume;
        stream->generation = request.generation;
        g.newStreams[index] = std::move(stream);
    }
}

// ---------------------------------------------------------------------------
// Mixer thread
// ---------------------------------------------------------------------------
std::vector<Voice> g_voices;                       // mixer thread only
std::unique_ptr<StreamState> g_streams[2];         // mixer thread only
std::vector<std::int32_t> g_accumulator(kFrames * 2);
std::vector<std::int32_t> g_voiceMix(kFrames * 2);

// Returns false when the voice has finished.
bool mixVoice(Voice &voice, std::int32_t *out)
{
    const Pcm &pcm = *voice.pcm;
    const std::size_t frames = pcm.frames();
    const std::int16_t *samples = pcm.samples.data();
    const int channels = pcm.channels;
    const float gl = voice.gainLeft;
    const float gr = voice.gainRight;
    for (int f = 0; f < kFrames; ++f)
    {
        const std::size_t index = static_cast<std::size_t>(voice.position);
        if (index + 1 >= frames)
            return false;
        const float t = static_cast<float>(voice.position - static_cast<double>(index));
        const std::int16_t *a = samples + index * channels;
        const std::int16_t *b = a + channels;
        const float left = a[0] + (b[0] - a[0]) * t;
        const float right = channels > 1 ? a[1] + (b[1] - a[1]) * t : left;
        out[f * 2] += static_cast<std::int32_t>(left * gl);
        out[f * 2 + 1] += static_cast<std::int32_t>(right * gr);
        voice.position += voice.step;
    }
    return true;
}

bool mixStream(StreamState &stream, float busVolume, std::int32_t *out)
{
    if (!stream.primed)
    {
        if (!stream.advance() || !stream.advance())
            return false;
        stream.primed = true;
    }
    const float gain = stream.volume * busVolume;
    for (int f = 0; f < kFrames; ++f)
    {
        const float t = static_cast<float>(stream.fraction);
        out[f * 2] += static_cast<std::int32_t>(
            (stream.previous[0] + (stream.current[0] - stream.previous[0]) * t) * gain);
        out[f * 2 + 1] += static_cast<std::int32_t>(
            (stream.previous[1] + (stream.current[1] - stream.previous[1]) * t) * gain);
        stream.fraction += stream.step;
        while (stream.fraction >= 1.0)
        {
            stream.fraction -= 1.0;
            if (!stream.advance())
                return false;
        }
    }
    return true;
}

void fillBuffer(std::int16_t *target)
{
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        for (Voice &voice : g.newVoices)
        {
            if (g_voices.size() >= kMaxVoices)
                break;
            g_voices.push_back(std::move(voice));
        }
        g.newVoices.clear();
        for (int i = 0; i < 2; ++i)
        {
            if (g.stopStreams[i])
            {
                g_streams[i].reset();
                g.stopStreams[i] = false;
            }
            if (g.newStreams[i])
                g_streams[i] = std::move(g.newStreams[i]);
        }
    }

    std::int32_t *acc = g_accumulator.data();
    std::fill(g_accumulator.begin(), g_accumulator.end(), 0);

    const float soundVolume = g.soundVolume.load(std::memory_order_relaxed);
    const float musicVolume = g.musicVolume.load(std::memory_order_relaxed);
    if (soundVolume > 0.0f)
    {
        // Voice gains carry the sound volume from when they were queued; the
        // bus volume is applied here so the slider acts immediately.
        std::vector<std::int32_t> &voiceMix = g_voiceMix;
        std::fill(voiceMix.begin(), voiceMix.end(), 0);
        for (std::size_t i = 0; i < g_voices.size();)
        {
            if (mixVoice(g_voices[i], voiceMix.data()))
                ++i;
            else
            {
                g_voices[i] = std::move(g_voices.back());
                g_voices.pop_back();
            }
        }
        for (int s = 0; s < kFrames * 2; ++s)
            acc[s] += static_cast<std::int32_t>(static_cast<float>(voiceMix[static_cast<std::size_t>(s)]) * soundVolume);
    }
    else
    {
        g_voices.clear();
    }

    const float streamBus[2] = {musicVolume, soundVolume};
    for (int i = 0; i < 2; ++i)
    {
        if (!g_streams[i])
            continue;
        if (!mixStream(*g_streams[i], streamBus[i], acc))
        {
            const std::uint32_t generation = g_streams[i]->generation;
            g_streams[i].reset();
            std::lock_guard<std::mutex> lock(g.mutex);
            // Only clear the flag if no newer stream was requested meanwhile.
            if (g.streamGeneration[i] == generation)
                g.streamActive[i].store(false);
        }
    }

    for (int s = 0; s < kFrames * 2; ++s)
        target[s] = static_cast<std::int16_t>(std::clamp(acc[s], -32768, 32767));
}

void mixerMain(void *)
{
    for (int i = 0; i < kBuffers; ++i)
    {
        fillBuffer(g.buffers[i]);
        audoutAppendAudioOutBuffer(&g.outBuffers[i]);
    }
    for (;;)
    {
        {
            std::lock_guard<std::mutex> lock(g.mutex);
            if (g.quit)
                return;
        }
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 100000000ULL)) || released == nullptr)
            continue;
        fillBuffer(static_cast<std::int16_t *>(released->buffer));
        audoutAppendAudioOutBuffer(released);
    }
}
}

bool start()
{
    if (g.running)
        return true;
    if (R_FAILED(audoutInitialize()))
        return false;
    if (R_FAILED(audoutStartAudioOut()))
    {
        audoutExit();
        return false;
    }
    for (int i = 0; i < kBuffers; ++i)
    {
        g.buffers[i] = static_cast<std::int16_t *>(std::aligned_alloc(0x1000, kBufferAlloc));
        std::memset(g.buffers[i], 0, kBufferAlloc);
        g.outBuffers[i].next = nullptr;
        g.outBuffers[i].buffer = g.buffers[i];
        g.outBuffers[i].buffer_size = kBufferAlloc;
        g.outBuffers[i].data_size = kBufferBytes;
        g.outBuffers[i].data_offset = 0;
    }
    g.quit = false;
    // Mixer above the game thread's priority (0x2C) and off its core, so a
    // long frame cannot starve the output. The loader is ordinary work.
    if (R_FAILED(threadCreate(&g.mixerThread, mixerMain, nullptr, nullptr, 0x8000, 0x2A, 2)))
    {
        audoutStopAudioOut();
        audoutExit();
        return false;
    }
    if (R_FAILED(threadCreate(&g.loaderThread, loaderMain, nullptr, nullptr, 0x20000, 0x2C, 2)))
    {
        threadClose(&g.mixerThread);
        audoutStopAudioOut();
        audoutExit();
        return false;
    }
    threadStart(&g.mixerThread);
    threadStart(&g.loaderThread);
    g.running = true;
    return true;
}

void stop()
{
    if (!g.running)
        return;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        g.quit = true;
        g.requests.clear();
    }
    g.loaderWake.notify_all();
    threadWaitForExit(&g.loaderThread);
    threadWaitForExit(&g.mixerThread);
    threadClose(&g.loaderThread);
    threadClose(&g.mixerThread);
    audoutStopAudioOut();
    audoutExit();
    g_voices.clear();
    g_streams[0].reset();
    g_streams[1].reset();
    g.newVoices.clear();
    g.newStreams[0].reset();
    g.newStreams[1].reset();
    g.streamActive[0].store(false);
    g.streamActive[1].store(false);
    for (std::int16_t *&buffer : g.buffers)
    {
        std::free(buffer);
        buffer = nullptr;
    }
    g.running = false;
}

bool running() { return g.running; }

void setVolumes(float sound, float music)
{
    g.soundVolume.store(std::clamp(sound, 0.0f, 1.0f), std::memory_order_relaxed);
    g.musicVolume.store(std::clamp(music, 0.0f, 1.0f), std::memory_order_relaxed);
}

void setListener(float x, float y, float z, float yawDegrees)
{
    g.listener[0] = x;
    g.listener[1] = y;
    g.listener[2] = z;
    g.yawRadians = yawDegrees * 0.017453292f;
}

void playSound(const std::string &path, float volume, float pitch, bool positional, float x, float y, float z)
{
    if (!g.running || volume <= 0.0f)
        return;
    Request request;
    request.kind = Request::Kind::Sound;
    request.path = path;
    request.volume = volume;
    request.pitch = pitch;
    if (positional)
    {
        // Pan by the source's side of the listener: right is (-cos yaw, -sin yaw)
        // in Minecraft's x/z. Centre plays at full gain on both sides.
        const float dx = x - g.listener[0];
        const float dz = z - g.listener[2];
        const float length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.001f)
        {
            const float pan = (dx * -std::cos(g.yawRadians) + dz * -std::sin(g.yawRadians)) / length;
            request.gainLeft = std::min(1.0f, 1.0f - pan * 0.7f);
            request.gainRight = std::min(1.0f, 1.0f + pan * 0.7f);
        }
    }
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        if (g.requests.size() > 64)
            return; // a burst the loader can't keep up with; drop
        g.requests.push_back(std::move(request));
    }
    g.loaderWake.notify_one();
}

void startStream(Stream stream, const std::string &path, float volume)
{
    if (!g.running)
        return;
    const int index = streamIndex(stream);
    Request request;
    request.kind = Request::Kind::Stream;
    request.path = path;
    request.volume = volume;
    request.stream = stream;
    {
        std::lock_guard<std::mutex> lock(g.mutex);
        request.generation = ++g.streamGeneration[index];
        g.newStreams[index].reset();
        g.stopStreams[index] = true;
        g.streamActive[index].store(true);
        g.requests.push_back(std::move(request));
    }
    g.loaderWake.notify_one();
}

void stopStream(Stream stream)
{
    if (!g.running)
        return;
    const int index = streamIndex(stream);
    std::lock_guard<std::mutex> lock(g.mutex);
    ++g.streamGeneration[index];
    g.newStreams[index].reset();
    g.stopStreams[index] = true;
    g.streamActive[index].store(false);
}

bool streamActive(Stream stream)
{
    return g.running && g.streamActive[streamIndex(stream)].load();
}
}
