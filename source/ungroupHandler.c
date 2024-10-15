
#include "ungroupHandler.h"

#include "app_timer.h"
#include "centralHandler.h"
#include "commands.h"
#include "flashHandler.h"
#include "LIS2DW12.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "popTimer.h"
#include "stdlib.h"
#include "system.h"

// Public Variables


// Private Variables
#define UNGROUP_TAP_COUNT			10
#define UNGROUP_TAP_THRESHOLD		19500
#define UNGROUP_TAP_GAP_TICKS_MIN	APP_TIMER_TICKS(170)
#define UNGROUP_TAP_GAP_TICKS_MAX	APP_TIMER_TICKS(10000)


// Private Function Declarations

// Private Function Definitions
static void monitorUnGrouping(void) {
	// Monitors accelerometer patterns to determine if user is attempting to ungroup this device.
	// User must tap device on something 10 times within 10 seconds.
	static int32_t lastAccel = 0;
	static int8_t tapCount = 0;
	static Timestamp_t nextTapTimeMin = {0};
	static Timestamp_t nextTapTimeMax = {0};

	// Do not ungroup in the middle of a Poptime session.
	if (getIsRunning()) {
		return;
	}

	XYZ_t xyz = getXYZ();
	int32_t accelNow = abs(xyz.x) + abs(xyz.y) + abs(xyz.z);
	int32_t diff = abs(accelNow - lastAccel);
	lastAccel = accelNow;

	if (diff >= UNGROUP_TAP_THRESHOLD) {
		Timestamp_t now = getNow();

		// See if surpassed max timeout.
		if (nextTapTimeMax.ticks < now.ticks) {
			// Clear any previous counts.
			tapCount = 0;
		}

		// See if met minimum time.
		if (nextTapTimeMin.ticks > now.ticks) {
			return;
		}

		tapCount++;
		nextTapTimeMin.ticks = (int64_t) now.ticks + UNGROUP_TAP_GAP_TICKS_MIN;

		// See if first tap
		if (tapCount == 1) {
			nextTapTimeMax.ticks = (int64_t) now.ticks + UNGROUP_TAP_GAP_TICKS_MAX;
		} else if (tapCount >= UNGROUP_TAP_COUNT) {
			tapCount = 0;

			debugPrint("Ungrouped Device xTaps");
			unGroup();
		}

		// Reset
		lastAccel = 0;
	}
}

void unGroupViaBLE(CommandPacket_Ungroup_t *packet) {
	// Ungroups and unowns.
	if ((storage.ownerId != UNOWNED_ID) && (storage.ownerId != packet->ownerId)) {
		debugPrint("Unable to ungroup. Not owned: packet: %d", packet->ownerId);
		return;
	}

	unGroup();
}

void unGroup(void) {
	// Ungroups and unowns.
	if (storage.groupId == UNGROUPED_ID) {
		debugPrint("Already UnGrouped");
		return;
	}

	debugPrint("Device UnGrouped");

	storage.ownerId = UNOWNED_ID;
	storage.groupId = UNGROUPED_ID;
	storage.catcherGloveFICR = 0;
	storage.catcherWristFICR = 0;
	storage.fielderGloveFICR = 0;

	updateFlash();
	sendDeviceInfo();
	setScanFilter();
}


// Public Function Definitions
void ungroupLoop(void) {
	monitorUnGrouping();
}
