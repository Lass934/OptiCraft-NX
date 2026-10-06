#pragma once

// Poll libnx once per frame and publish both the platform snapshots and the
// LWJGL-compatible events consumed by the shared menus/gameplay code.
void switchInputPoll();

// GuiContainer reports when an inventory/container screen is open, so Y and X
// can act as Legacy Console's split-stack and quick-move buttons there.
void switchSetContainerScreen(bool open);
// True once for a left click issued by X: treat it as shift-click (quick move).
bool switchConsumeQuickMoveClick();
// One slot-to-slot step requested inside a container (-1/0/1 per axis).
bool switchConsumeSlotStep(int &dirX, int &dirY);

#include <string>

// Shows the system software keyboard (swkbd) and blocks until it closes.
// Returns true with the typed UTF-8 text in `out` when the player confirms,
// false when they cancel or the applet cannot be shown.
bool switchShowKeyboard(const std::string &initialText, int maxLength,
                        const char *guideText, std::string &out);
