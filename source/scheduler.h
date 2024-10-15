#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "ble.h"
#include "commands.h"

typedef union ScheduledEvents_t {
	ble_evt_t p_ble_evt;
	ble_gap_evt_adv_report_t p_adv_report;
	CommandPacket_t cmdPacket;

	// Any event types here for every parameter being put on app sceduler
} scheduledEvents;

#define SCHEDULED_EVENT_DATA_MAX_SIZE	sizeof(scheduledEvents)
#define SCHEDULED_QUEUE_SIZE	24 // Maximum number of events in queue

// Public Function Declarations
void schedulerInit(void);

#endif  // SCHEDULER_H
