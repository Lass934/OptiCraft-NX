// Native Nintendo Switch Homebrew bring-up. This is intentionally independent
// from the Wii and PS2 ports: it validates libnx services before game backends
// are introduced.
#include <switch.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>

namespace
{
std::uint32_t resultModule(Result result)
{
    return R_MODULE(result);
}

std::uint32_t resultDescription(Result result)
{
    return R_DESCRIPTION(result);
}

void printResult(const char* label, Result result)
{
    std::printf("%s: %s (0x%08X; module %u, description %u)\n",
                label,
                R_SUCCEEDED(result) ? "OK" : "FAILED",
                static_cast<unsigned int>(result),
                resultModule(result),
                resultDescription(result));
}

void printDevoptabStatus()
{
    // libnx's default runtime initializes FS and mounts "sdmc:" before main()
    // runs.  Calling fsdevMountSdmc() here would therefore attempt a second
    // registration of the same devoptab device and can report a misleading
    // libnx error even when the SD card is usable.
    errno = 0;
    DIR* const directory = opendir("sdmc:/");
    if (directory != nullptr)
    {
        closedir(directory);
        std::printf("SD devoptab access: OK\n");
        return;
    }

    const int error = errno;
    std::printf("SD devoptab access: FAILED (errno %d: %s)\n",
                error,
                std::strerror(error));
}
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    consoleInit(nullptr);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    // Check Horizon's SD-card filesystem service separately from the already
    // mounted POSIX devoptab. This distinguishes a console/SD service failure
    // from a failure in fsdev's path adapter without mounting sdmc: twice.
    FsFileSystem sdCardFileSystem;
    const Result nativeFsResult = fsOpenSdCardFileSystem(&sdCardFileSystem);
    if (R_SUCCEEDED(nativeFsResult))
        fsFsClose(&sdCardFileSystem);

    char cwd[PATH_MAX] = "(unavailable)";
    if (getcwd(cwd, sizeof(cwd)) == nullptr)
        std::snprintf(cwd, sizeof(cwd), "(unavailable)");

    std::printf("OptiCraft Heritage - Switch bring-up\n");
    std::printf("===================================\n\n");
    std::printf("libnx initialized successfully.\n");
    printResult("Native SD filesystem", nativeFsResult);
    printDevoptabStatus();
    std::printf("Working directory: %s\n\n", cwd);
    std::printf("Press PLUS to exit.\n");

    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
