#pragma once

#include "GuiScreen.h"

class Container;
class Slot;
class RenderItem;
class InventoryPlayer;

// net.minecraft.src.GuiContainer
class GuiContainer : public GuiScreen
{
public:
	GuiContainer(Container *container, bool ownsContainer = false);
	virtual ~GuiContainer();

	void initGui() override;
	void drawScreen(int_t mouseX, int_t mouseY, float_t partialTick) override;

protected:
	virtual void drawGuiContainerForegroundLayer();
	virtual void drawGuiContainerBackgroundLayer(float_t partialTick) = 0;

private:
	void drawSlotInventory(Slot *slot);

public:
	// Public for the console D-pad slot navigation (ContainerSlotNavigator),
	// which has to ask which slot the simulated cursor is over before it can
	// step to the next one. Sharing the click path's own hit test is what keeps
	// the two from disagreeing about where a slot ends.
	Slot *getSlotAtPosition(int_t mouseX, int_t mouseY);
	// A container can keep controller navigation inside a specialized slot grid.
	// Returning nullptr uses the normal geometric navigation.
	virtual Slot *getControllerNavigationTarget(Slot *selected, int_t dirX, int_t dirY);

#ifdef SWITCH_PLATFORM
private:
	// Legacy Console slot-to-slot cursor: snaps to a slot when the screen opens
	// and steps to the neighbouring slot for each requested direction.
	void switchUpdateSlotCursor(int_t mouseX, int_t mouseY);
	void switchMoveCursorToSlot(Slot *slot);
	Slot *switchNearestSlot(int_t x, int_t y) const;
	bool switchCursorSnapped = false;
public:
#endif

private:
	bool getIsMouseOverSlot(Slot *slot, int_t mouseX, int_t mouseY);

protected:
	void mouseClicked(int_t x, int_t y, int_t button) override;
	virtual void handleMouseClick(Slot *slot, int_t slotId, int_t button, bool shift);
	void mouseMovedOrUp(int_t x, int_t y, int_t button) override;
	void keyTyped(char_t c, int_t key) override;

public:
	void onGuiClosed() override;
	bool doesGuiPauseGame() override;
	void updateScreen() override;
	bool allowsPlatformPointerInput() const override { return true; }

private:
	static RenderItem *itemRenderer;

protected:
	int_t xSize;
	int_t ySize;
	int_t guiLeft;
	int_t guiTop;

public:
	Container *inventorySlots;

private:
	bool ownsInventorySlots;
};
