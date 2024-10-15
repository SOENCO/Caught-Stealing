
#include "scheduler.h"
#include "app_scheduler.h"

void schedulerInit(void) {
	APP_SCHED_INIT(SCHEDULED_EVENT_DATA_MAX_SIZE, SCHEDULED_QUEUE_SIZE);
}
