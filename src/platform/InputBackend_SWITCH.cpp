#include "platform/Input.h"

#include "lwjgl/Display.h"
#include "lwjgl/Keyboard.h"
#include "lwjgl/Mouse.h"
#include "switch/input/SwitchInput.h"
#include "switch/SwitchRuntimeDebug.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <switch.h>

namespace
{
PadState g_pad;
bool g_initialized = false;
u64 g_buttons = 0;
u64 g_pressed = 0;
PlatformGamepadSnapshot g_gamepad;
int g_cursorX = 640;
int g_cursorY = 360;

constexpr float kStickScale = 1.0f / 32768.0f;
constexpr float kPointerDeadzone = 0.18f;
constexpr float kPointerSpeed = 18.0f;

float axis(s32 value)
{
    return std::clamp(static_cast<float>(value) * kStickScale, -1.0f, 1.0f);
}

void initialize()
{
    if (g_initialized) return;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    g_initialized = true;
}

std::uint32_t textActions(u64 buttons)
{
    std::uint32_t result = 0;
    if (buttons & HidNpadButton_Left)  result |= PLATFORM_TEXT_LEFT;
    if (buttons & HidNpadButton_Right) result |= PLATFORM_TEXT_RIGHT;
    if (buttons & HidNpadButton_Up)    result |= PLATFORM_TEXT_UP;
    if (buttons & HidNpadButton_Down)  result |= PLATFORM_TEXT_DOWN;
    if (buttons & HidNpadButton_A)     result |= PLATFORM_TEXT_TYPE;
    if (buttons & HidNpadButton_X)     result |= PLATFORM_TEXT_BACK;
    if (buttons & HidNpadButton_Minus) result |= PLATFORM_TEXT_SPACE;
    if (buttons & HidNpadButton_Y)     result |= PLATFORM_TEXT_SHIFT;
    if (buttons & HidNpadButton_Plus)  result |= PLATFORM_TEXT_ENTER;
    if (buttons & HidNpadButton_B)     result |= PLATFORM_TEXT_CLOSE;
    return result;
}

// Legacy Console Edition (Nintendo Switch Edition) layout. A/B follow their
// labels; X/Y follow the Xbox positions, so Y is crafting / split stack.
// Gameplay (mouse grabbed):
//   A jump   B drop   X inventory   Y crafting (the 2x2 grid lives in the
//   1.2.5 inventory, so it opens the inventory)   ZR mine   ZL use/place
//   L / R previous / next hotbar slot   L3 sneak (toggle)   R3 camera view
//   + pause   - debug overlay (F3; singleplayer has no player list)
//   Sprint: push the left stick forward twice, as on console.
// Menus: A click, B / + back, either stick moves the cursor, D-pad arrow keys,
// L / R scroll. In inventories and containers: A pick up / place all,
// Y split stack / place one, X quick move.
struct KeyBinding
{
    u64 button;
    int gameplayKey; // 0 = nothing while playing
    int menuKey;     // 0 = nothing in menus
};

constexpr KeyBinding kKeys[] = {
    {HidNpadButton_A, lwjgl::Keyboard::KEY_SPACE, 0},
    {HidNpadButton_B, lwjgl::Keyboard::KEY_Q, lwjgl::Keyboard::KEY_ESCAPE},
    {HidNpadButton_X, lwjgl::Keyboard::KEY_E, 0},
    {HidNpadButton_Y, lwjgl::Keyboard::KEY_E, 0},
    {HidNpadButton_StickR, lwjgl::Keyboard::KEY_F5, 0},
    {HidNpadButton_Plus, lwjgl::Keyboard::KEY_ESCAPE, lwjgl::Keyboard::KEY_ESCAPE},
    {HidNpadButton_Minus, lwjgl::Keyboard::KEY_F3, lwjgl::Keyboard::KEY_F3},
    {HidNpadButton_Up, 0, lwjgl::Keyboard::KEY_UP},
    {HidNpadButton_Down, 0, lwjgl::Keyboard::KEY_DOWN},
    {HidNpadButton_Left, 0, lwjgl::Keyboard::KEY_LEFT},
    {HidNpadButton_Right, 0, lwjgl::Keyboard::KEY_RIGHT},
};
constexpr std::size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

// The key each button actually pressed, so its release goes to the same key
// even if a menu opened or closed in between (B pressed in game = drop, but
// released while the inventory is open must still release Q, not Escape).
int g_heldKey[kKeyCount] = {};

// L3 toggles sneak like the console editions; the sneak binding is LSHIFT.
bool g_sneakToggled = false;

// Set by GuiContainer while an inventory/container screen is open.
bool g_containerOpen = false;
// The next left click comes from X: GuiContainer treats it as shift-click.
bool g_quickMoveClick = false;
// Mouse button each of Y / X pressed inside a container, released on release.
int g_yButton = -1;
int g_xButton = -1;

// Inside containers the sticks and D-pad move the cursor slot by slot, like
// Legacy Console: one step when a direction is first pushed, then repeats
// while it is held. GuiContainer consumes the step and picks the target slot.
constexpr float kSlotStickThreshold = 0.5f;
constexpr u64 kSlotRepeatDelayNs = 260000000ULL;
constexpr u64 kSlotRepeatIntervalNs = 110000000ULL;
int g_slotDirX = 0;
int g_slotDirY = 0;
u64 g_slotNextRepeat = 0;
int g_slotStepX = 0;
int g_slotStepY = 0;

void updateSlotStep(u64 buttons)
{
    int dirX = 0;
    int dirY = 0;
    if (buttons & HidNpadButton_Left) dirX = -1;
    else if (buttons & HidNpadButton_Right) dirX = 1;
    else if (buttons & HidNpadButton_Up) dirY = -1;
    else if (buttons & HidNpadButton_Down) dirY = 1;
    else
    {
        // Either stick; the dominant axis wins so diagonals pick one direction.
        float x = g_gamepad.leftX, y = g_gamepad.leftY;
        if (std::abs(g_gamepad.rightX) + std::abs(g_gamepad.rightY) > std::abs(x) + std::abs(y))
        {
            x = g_gamepad.rightX;
            y = g_gamepad.rightY;
        }
        if (std::abs(x) >= std::abs(y) && std::abs(x) >= kSlotStickThreshold) dirX = x < 0.0f ? -1 : 1;
        else if (std::abs(y) >= kSlotStickThreshold) dirY = y < 0.0f ? -1 : 1;
    }

    const u64 now = armTicksToNs(armGetSystemTick());
    if (dirX == 0 && dirY == 0)
    {
        g_slotDirX = g_slotDirY = 0;
        return;
    }
    if (dirX != g_slotDirX || dirY != g_slotDirY)
    {
        g_slotDirX = dirX;
        g_slotDirY = dirY;
        g_slotStepX = dirX;
        g_slotStepY = dirY;
        g_slotNextRepeat = now + kSlotRepeatDelayNs;
        return;
    }
    if (now >= g_slotNextRepeat)
    {
        g_slotStepX = dirX;
        g_slotStepY = dirY;
        g_slotNextRepeat = now + kSlotRepeatIntervalNs;
    }
}

void setSneak(bool on)
{
    if (g_sneakToggled == on) return;
    g_sneakToggled = on;
    lwjgl::Keyboard::detail::pushKey(lwjgl::Keyboard::KEY_LSHIFT, on);
}

void emitKeyChanges(u64 down, u64 up)
{
    if (platformTextInputExclusive()) return;
    const bool gameplay = lwjgl::Mouse::isGrabbed();
    for (std::size_t i = 0; i < kKeyCount; ++i)
    {
        const KeyBinding &binding = kKeys[i];
        if ((up & binding.button) && g_heldKey[i] != 0)
        {
            lwjgl::Keyboard::detail::pushKey(g_heldKey[i], false);
            g_heldKey[i] = 0;
        }
        if (down & binding.button)
        {
            const int key = gameplay ? binding.gameplayKey : binding.menuKey;
            if (key != 0)
            {
                lwjgl::Keyboard::detail::pushKey(key, true);
                g_heldKey[i] = key;
            }
        }
    }

    if (!gameplay)
    {
        // Leaving gameplay (inventory, pause) drops a toggled sneak, as the
        // console editions do, so the player is not stuck crouching.
        setSneak(false);
        return;
    }
    if (down & HidNpadButton_StickL)
        setSneak(!g_sneakToggled);
}

void emitHotbar(u64 down)
{
    // Mouse wheel: positive selects the previous slot, negative the next.
    // In menus the same wheel scrolls lists.
    int wheel = 0;
    if (down & HidNpadButton_L) ++wheel;
    if (down & HidNpadButton_R) --wheel;
    if (wheel != 0)
        lwjgl::Mouse::detail::pushWheel(wheel, g_cursorX, g_cursorY);
}

void emitMouseButton(u64 down, u64 up, u64 mask, int button)
{
    if (down & mask) lwjgl::Mouse::detail::pushButton(button, true, g_cursorX, g_cursorY);
    if (up & mask) lwjgl::Mouse::detail::pushButton(button, false, g_cursorX, g_cursorY);
}
}

void switchInputPoll()
{
    initialize();
    padUpdate(&g_pad);

    g_buttons = padGetButtons(&g_pad);
    g_pressed = padGetButtonsDown(&g_pad);
    const u64 released = padGetButtonsUp(&g_pad);
    const HidAnalogStickState left = padGetStickPos(&g_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&g_pad, 1);

    g_gamepad.connected = padIsConnected(&g_pad);
    g_gamepad.leftX = axis(left.x);
    g_gamepad.leftY = -axis(left.y);
    g_gamepad.rightX = axis(right.x);
    g_gamepad.rightY = -axis(right.y);

    emitKeyChanges(g_pressed, released);
    emitHotbar(g_pressed);
    if (!lwjgl::Mouse::isGrabbed())
        emitMouseButton(g_pressed, released, HidNpadButton_A, 0);
    emitMouseButton(g_pressed, released, HidNpadButton_ZR, 0);
    emitMouseButton(g_pressed, released, HidNpadButton_ZL, 1);
    if (g_containerOpen && !lwjgl::Mouse::isGrabbed())
    {
        // Y: right click (split a stack / place one). X: shift-click.
        if (g_pressed & HidNpadButton_Y)
        {
            g_yButton = 1;
            lwjgl::Mouse::detail::pushButton(1, true, g_cursorX, g_cursorY);
        }
        if (g_pressed & HidNpadButton_X)
        {
            g_xButton = 0;
            g_quickMoveClick = true;
            lwjgl::Mouse::detail::pushButton(0, true, g_cursorX, g_cursorY);
        }
    }
    if ((released & HidNpadButton_Y) && g_yButton >= 0)
    {
        lwjgl::Mouse::detail::pushButton(g_yButton, false, g_cursorX, g_cursorY);
        g_yButton = -1;
    }
    if ((released & HidNpadButton_X) && g_xButton >= 0)
    {
        lwjgl::Mouse::detail::pushButton(g_xButton, false, g_cursorX, g_cursorY);
        g_xButton = -1;
    }

    if (g_containerOpen && !lwjgl::Mouse::isGrabbed())
    {
        updateSlotStep(g_buttons);
        return;
    }
    g_slotDirX = g_slotDirY = 0;

    // In menus either stick drives the cursor (the right stick wins when both
    // are deflected). In gameplay only the right stick is the camera.
    float stickX = g_gamepad.rightX;
    float stickY = g_gamepad.rightY;
    if (!lwjgl::Mouse::isGrabbed() &&
        std::abs(stickX) < kPointerDeadzone && std::abs(stickY) < kPointerDeadzone)
    {
        stickX = g_gamepad.leftX;
        stickY = g_gamepad.leftY;
    }
    const float pointerX = std::abs(stickX) >= kPointerDeadzone ? stickX : 0.0f;
    const float pointerY = std::abs(stickY) >= kPointerDeadzone ? stickY : 0.0f;
    const int dx = static_cast<int>(std::lround(pointerX * kPointerSpeed));
    const int dy = static_cast<int>(std::lround(pointerY * kPointerSpeed));
    if (dx != 0 || dy != 0)
    {
        g_cursorX = std::clamp(g_cursorX + dx, 0, lwjgl::Display::getWidth() - 1);
        g_cursorY = std::clamp(g_cursorY + dy, 0, lwjgl::Display::getHeight() - 1);
        lwjgl::Mouse::detail::pushMotion(g_cursorX, g_cursorY, dx, dy);
    }
}

PlatformTextInputSnapshot platformTextInputSnapshot(int)
{
    initialize();
    PlatformTextInputSnapshot result;
    result.connected = padIsConnected(&g_pad);
    result.held = textActions(g_buttons);
    result.pressed = textActions(g_pressed);
    result.pointerValid = result.connected;
    result.pointerX = g_cursorX;
    result.pointerY = g_cursorY;
    result.pointerWidth = lwjgl::Display::getWidth();
    result.pointerHeight = lwjgl::Display::getHeight();
    return result;
}

PlatformGamepadSnapshot platformRawGamepadSnapshot(int)
{
    initialize();
    return g_gamepad;
}

PlatformGamepadSnapshot platformGamepadSnapshot(int port)
{
    return platformRawGamepadSnapshot(port);
}

int platformMenuPad() { return 0; }
bool platformMenuPointerActive() { return true; }
bool platformMenuCursorVisible() { return true; }

void platformSetMenuCursor(int x, int y)
{
    g_cursorX = std::clamp(x, 0, lwjgl::Display::getWidth() - 1);
    g_cursorY = std::clamp(y, 0, lwjgl::Display::getHeight() - 1);
    lwjgl::Mouse::setCursorPosition(g_cursorX, g_cursorY);
}

const PlatformKeyboardHints &platformKeyboardHints()
{
    static const PlatformKeyboardHints hints = {
        {"A:type  X:del  Y:shift  -:space  +:ok  B:close", nullptr, nullptr}, 1};
    return hints;
}

const char *platformInputDebugLine()
{
    return g_gamepad.connected ? "Joy-Con / Pro Controller connected" : "Controller disconnected";
}

bool switchShowKeyboard(const std::string &initialText, int maxLength,
                        const char *guideText, std::string &out)
{
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetInitialText(&keyboard, initialText.c_str());
    swkbdConfigSetInitialCursorPos(&keyboard, 1); // 1 = cursor at the end
    if (guideText != nullptr)
        swkbdConfigSetGuideText(&keyboard, guideText);
    if (maxLength > 0)
        swkbdConfigSetStringLenMax(&keyboard, static_cast<u32>(maxLength));

    // The applet takes over the screen and this thread waits for it; tell the
    // stall watchdog this is not a hang.
    switchDebugSuspendWatchdog(true);
    char buffer[512] = {};
    const Result shown = swkbdShow(&keyboard, buffer, sizeof(buffer));
    switchDebugSuspendWatchdog(false);
    swkbdClose(&keyboard);
    if (R_FAILED(shown))
        return false; // cancelled (B / X on the keyboard) or unavailable
    out = buffer;
    return true;
}

void switchSetContainerScreen(bool open)
{
    g_containerOpen = open;
    g_slotStepX = g_slotStepY = 0;
    g_slotDirX = g_slotDirY = 0;
    if (!open)
        g_quickMoveClick = false;
}

bool switchConsumeSlotStep(int &dirX, int &dirY)
{
    dirX = g_slotStepX;
    dirY = g_slotStepY;
    g_slotStepX = g_slotStepY = 0;
    return dirX != 0 || dirY != 0;
}

bool switchConsumeQuickMoveClick()
{
    const bool quickMove = g_quickMoveClick;
    g_quickMoveClick = false;
    return quickMove;
}
