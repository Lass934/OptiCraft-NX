// Resource_wii.cpp — Wii implementation of Resource::getResource().
//
// Wii resources live on the libfat filesystem instead of inside the DOL. A DOL
// is loaded whole into the scarce MEM1, so embedding ~10 MB of assets would spend
// memory to save nothing. Resources are read from the SD card or USB drive the
// game was installed on (GameResources::getExeDir() resolves which -- see
// WiiEarlyInit.cpp), which also means they can be replaced without rebuilding.
//
// Callers ask for paths rooted at the resource directory ("/terrain.png",
// "/gui/logo.png"), matching the desktop contract, so the leading slash is
// dropped and the rest is resolved under <app>/data/assets.
//
// The returned stream is owned by the caller, as on every other platform.
#ifdef SWITCH_PLATFORM

#include "java/Resource.h"
#include "java/String.h"
#include "net/minecraft/src/GameResources.h"
#include "platform/storage/PathUtils.h"

#include <stdexcept>
#include <string>

namespace Resource
{

std::istream *getResource(const jstring &name)
{
	// This runs from static initialisers (SharedConstants, ChatAllowedCharacters)
	// long before main(), so the filesystem cannot be assumed to be up yet.

	auto input = GameResources::open(static_cast<const std::string &>(name));
	if (!input)
	{
		const std::string path = PlatformStorage::join(
			GameResources::getAssetsDir(), static_cast<const std::string &>(name));
		throw std::runtime_error(
			"Missing game resource:\n" + path +
			"Expected runtime assets under sdmc:/switch/OptiCraft/data/assets");
	}

	return input.release();
}

} // namespace Resource

#endif // SWITCH_PLATFORM
