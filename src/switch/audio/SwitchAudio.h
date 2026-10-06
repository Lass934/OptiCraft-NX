#pragma once

#include <string>

// Software mixer on audout (48 kHz, stereo, s16). Sounds are OGG files decoded
// by a loader thread and cached; music and records stream from disk. A mixer
// thread at a higher priority than the game, on its own core, fills the
// output buffers, so audio keeps playing through a slow frame. Every function
// here is called from the game thread and only queues work.
namespace SwitchAudio
{
enum class Stream
{
    Music,  // background music, on the music volume
    Record  // jukebox records ("streaming" in SoundManager), on the sound volume
};

bool start();
void stop();
bool running();

void setVolumes(float sound, float music);
// Listener position and facing (Minecraft yaw, degrees), for panning.
void setListener(float x, float y, float z, float yawDegrees);

// volume is final (distance attenuation already applied); pitch 1 = normal.
// A positional sound is panned by its direction from the listener.
void playSound(const std::string &path, float volume, float pitch,
               bool positional, float x, float y, float z);

void startStream(Stream stream, const std::string &path, float volume);
void stopStream(Stream stream);
// True from startStream until the stream ends or is stopped.
bool streamActive(Stream stream);
}
