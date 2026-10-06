#include "net/minecraft/src/SoundManager.h"
#include "platform/Log.h"

#if defined(NO_SOUND)


void *SoundManager::sndSystem = nullptr;
bool  SoundManager::loaded    = false;

SoundManager::SoundManager()
    : soundPoolSounds(), soundPoolStreaming(), soundPoolMusic(), soundVolume(0),
      options(nullptr), rand(), ticksBeforeMusic(0)
{
}

void SoundManager::loadSoundSettings(GameSettings *gamesettings) { options = gamesettings; loaded = false; }
void SoundManager::onSoundOptionsChanged() {}
void SoundManager::closeMinecraft() { loaded = false; }
void SoundManager::addSound(const jstring &s, const std::string &file) { soundPoolSounds.addSound(s, file); }
void SoundManager::addStreaming(const jstring &s, const std::string &file) { soundPoolStreaming.addSound(s, file); }
void SoundManager::addMusic(const jstring &s, const std::string &file) { soundPoolMusic.addSound(s, file); }
void SoundManager::playRandomMusicIfReady() {}
bool SoundManager::playMusicFileNow(const std::string &) { return false; }
void SoundManager::setListenerPosition(EntityLiving *, float) {}
void SoundManager::playStreaming(const jstring &, float, float, float, float, float) {}
void SoundManager::playSound(const jstring &, float, float, float, float, float) {}
void SoundManager::playSoundFX(const jstring &, float, float) {}
void SoundManager::tryToSetLibraryAndCodecs() {}


#else

// Switch backend: SwitchAudio mixes on audout. This file keeps the game-side
// rules of the vanilla SoundManager (random sound per name, distance
// attenuation, music every 10-20 minutes, records replacing the music).

#include <algorithm>

#include "net/minecraft/src/EntityLiving.h"
#include "net/minecraft/src/GameSettings.h"
#include "net/minecraft/src/SoundPoolEntry.h"
#include "platform/audio/AudioAssetFormat.h"
#include "platform/audio/AudioSpatialization.h"
#include "switch/audio/SwitchAudio.h"

namespace
{
AudioListenerState s_listener;
}

void *SoundManager::sndSystem = nullptr;
bool  SoundManager::loaded    = false;

SoundManager::SoundManager()
    : soundPoolSounds(), soundPoolStreaming(), soundPoolMusic(), soundVolume(0),
      options(nullptr), rand(), ticksBeforeMusic(rand.nextInt(12000))
{
}

void SoundManager::loadSoundSettings(GameSettings *gamesettings)
{
    soundPoolStreaming.motionX = false;
    options = gamesettings;
    if (!loaded && (gamesettings == nullptr || gamesettings->soundVolume != 0.0f || gamesettings->musicVolume != 0.0f))
        tryToSetLibraryAndCodecs();
    if (loaded)
        SwitchAudio::setVolumes(options ? options->soundVolume : 1.0f, options ? options->musicVolume : 1.0f);
}

void SoundManager::tryToSetLibraryAndCodecs()
{
    if (loaded)
        return;
    loaded = SwitchAudio::start();
    if (!loaded)
        MC_LOG_ERROR("audio", "audout could not be started\n");
}

void SoundManager::onSoundOptionsChanged()
{
    if (!loaded && options && (options->soundVolume != 0.0f || options->musicVolume != 0.0f))
        tryToSetLibraryAndCodecs();
    if (!loaded)
        return;
    SwitchAudio::setVolumes(options ? options->soundVolume : 1.0f, options ? options->musicVolume : 1.0f);
    // As in Java: Music at zero stops the background music only.
    if (options && options->musicVolume == 0.0f)
        SwitchAudio::stopStream(SwitchAudio::Stream::Music);
}

void SoundManager::closeMinecraft()
{
    SwitchAudio::stop();
    loaded = false;
}

void SoundManager::addSound(const jstring &s, const std::string &file)
{
    if (audioPathHasExtension(file, ".ogg"))
        soundPoolSounds.addSound(s, file);
}

void SoundManager::addStreaming(const jstring &s, const std::string &file)
{
    if (audioPathHasExtension(file, ".ogg"))
        soundPoolStreaming.addSound(s, file);
}

void SoundManager::addMusic(const jstring &s, const std::string &file)
{
    if (audioPathHasExtension(file, ".ogg"))
        soundPoolMusic.addSound(s, file);
}

bool SoundManager::playMusicFileNow(const std::string &file)
{
    if (!loaded || !options || options->musicVolume == 0.0f)
        return false;
    if (SwitchAudio::streamActive(SwitchAudio::Stream::Record) || !audioPathHasExtension(file, ".ogg"))
        return false;
    SwitchAudio::startStream(SwitchAudio::Stream::Music, file, 1.0f);
    ticksBeforeMusic = rand.nextInt(12000) + 12000;
    return true;
}

void SoundManager::playRandomMusicIfReady()
{
    if (!loaded || !options || options->musicVolume == 0.0f)
        return;
    if (SwitchAudio::streamActive(SwitchAudio::Stream::Music) ||
        SwitchAudio::streamActive(SwitchAudio::Stream::Record))
        return;
    if (ticksBeforeMusic > 0)
    {
        ticksBeforeMusic--;
        return;
    }
    SoundPoolEntry *entry = soundPoolMusic.getRandomSound();
    ticksBeforeMusic = rand.nextInt(12000) + 12000;
    if (entry != nullptr)
        SwitchAudio::startStream(SwitchAudio::Stream::Music, entry->soundUrl, 1.0f);
}

void SoundManager::setListenerPosition(EntityLiving *entityliving, float f)
{
    if (!loaded || entityliving == nullptr)
        return;
    updateAudioListener(s_listener, entityliving, f);
    const float yaw = entityliving->prevRotationYaw + (entityliving->rotationYaw - entityliving->prevRotationYaw) * f;
    SwitchAudio::setListener(s_listener.x, s_listener.y, s_listener.z, yaw);
}

void SoundManager::playStreaming(const jstring &s, float f, float f1, float f2, float f3, float)
{
    if (!loaded || !options || (options->soundVolume == 0.0f && !s.empty()))
        return;
    // Java uses one source named "streaming"; a new record replaces it, and
    // an empty name just stops it.
    SwitchAudio::stopStream(SwitchAudio::Stream::Record);
    if (s.empty())
        return;
    SoundPoolEntry *entry = soundPoolStreaming.getRandomSoundFromSoundPool(s);
    if (entry == nullptr || f3 <= 0.0f)
        return;
    const float attenuation = audioStreamingAttenuation(s_listener, f, f1, f2);
    if (attenuation <= 0.0f)
        return;
    // A record temporarily replaces the background music.
    SwitchAudio::stopStream(SwitchAudio::Stream::Music);
    ticksBeforeMusic = rand.nextInt(12000) + 12000;
    SwitchAudio::startStream(SwitchAudio::Stream::Record, entry->soundUrl, 0.5f * attenuation);
}

void SoundManager::playSound(const jstring &s, float f, float f1, float f2, float f3, float f4)
{
    if (!loaded || !options || options->soundVolume == 0.0f)
        return;
    SoundPoolEntry *entry = soundPoolSounds.getRandomSoundFromSoundPool(s);
    if (entry == nullptr || f3 <= 0.0f)
        return;
    const float attenuation = audioSpatialAttenuation(s_listener, f, f1, f2, f3);
    if (attenuation <= 0.0f)
        return;
    SwitchAudio::playSound(entry->soundUrl, std::min(f3, 1.0f) * attenuation, f4, true, f, f1, f2);
}

void SoundManager::playSoundFX(const jstring &s, float f, float f1)
{
    if (!loaded || !options || options->soundVolume == 0.0f)
        return;
    SoundPoolEntry *entry = soundPoolSounds.getRandomSoundFromSoundPool(s);
    if (entry != nullptr)
        SwitchAudio::playSound(entry->soundUrl, std::min(f, 1.0f) * 0.25f, f1, false, 0.0f, 0.0f, 0.0f);
}

#endif
