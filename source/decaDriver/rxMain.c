
#include "rxMain.h"

#include "app_scheduler.h"
#include "app_timer.h"
#include "commands.h"
#include "nrf_delay.h"
#include "nrf_drv_systick.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_gpio.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "popTimer.h"
#include <math.h>


// Public Variables


// Private Variables
#define DISTANCES_COUNT		10	// Distance is polled at 50 msec, therefore 50 x 10 is 500 msec worth of captured distances.
#define DISTANCE_TOLERANCE	0.25
static float distanceMeters[DISTANCES_COUNT] = {0};
static uint8_t distanceIndex = 0;
static CompleteSequence_t thisSequence = { 0 };

// Delay between frames, in UWB microseconds. See NOTE 4 below.
// This is the delay from Frame RX timestamp to TX reply timestamp used for calculating/setting the DW1000's delayed TX function. This includes the frame length of approximately 2.46 ms with above configuration.
#define POLL_RX_TO_RESP_TX_DLY_UUS 2750
#define DISTANCE_STREAM_TICKS	APP_TIMER_TICKS(300)


// Private Function Declarations
static void resetRX(void);
static float calculateDistance(CompleteSequence_t *sequence);
static void txDone(const dwt_cb_data_t *cbData);
static void rxOk(const dwt_cb_data_t *cbData);
static void rxTimeout(const dwt_cb_data_t *cbData);
static void rxError(const dwt_cb_data_t *cbData);
static void saveDistance(float meters);

// Private Function Definitions
static void resetRX(void) {
	// Clears errors and starts listening.
	clearErrors();

	// Disable RX timeout.
	dwt_setrxtimeout(0);
	// Start listening on RX.
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);
}

static float calculateDistance(CompleteSequence_t *sequence) {
	// Compute time of flight. 32-bit subtractions give correct answers even if clock has wrapped.
	static uint8_t buffer[120] = {0};

	uint32_t ptx = sequence->pollTx;
	uint64_t prx = sequence->pollRx;
	uint64_t rtx = sequence->replyTx;
	uint32_t rrx = sequence->replyRx;
	uint32_t ftx = sequence->finalTx;
	uint64_t frx = sequence->finalRx;

	int32_t Ra = rrx - ptx;
	int64_t Rb = frx - rtx;
	int32_t Da = ftx - rrx;
	int64_t Db = rtx - prx;

	//uint32_t tofDwtUnits = (uint32)( (((uint32) Ra * Rb) - ((uint32) Da * Db)) / ((uint32) Ra + Rb + Da + Db));
	int64_t tofDwtUnits = ((int64_t) Ra * Rb) - ((int64_t) Da * Db);
	tofDwtUnits = tofDwtUnits / ((int64_t) Ra + Rb + Da + Db);

	double tof = tofDwtUnits * (double) DWT_TIME_UNITS;
	float distance = (float) tof * SPEED_OF_LIGHT;

	if ((distance < 0) || (distance > 100)) {
		NRF_LOG_INFO("Distance Error: %d", distance);
	}

	if (distance < 0.0){
		distance = 0.0;
	}

	// NRF_LOG_INFO("pollTx:%lu    pollRx:%llu    replyTx:%llu     replyRx:%lu     finalTx:%lu     finalRx:%llu", sequence->pollTx, sequence->pollRx, sequence->replyTx, sequence->replyRx, sequence->finalTx, sequence->finalRx);
	// sprintf(buffer, "Ra: %ld   Rb: %lld   Da: %ld   Db: %lld ", (long)Ra, (long long)Rb, (long) Da, (long long)Db);
	// sprintf(&buffer[strlen(buffer)], "Distance:" NRF_LOG_FLOAT_MARKER " m", NRF_LOG_FLOAT(distance));
	// NRF_LOG_INFO("\t%s", buffer);

	return distance;
}

static void txDone(const dwt_cb_data_t *cbData) {
	//NRF_LOG_DEBUG("txDone");

	dwt_setrxtimeout(0);
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);
}

static void rxOk(const dwt_cb_data_t *cbData) {
	//NRF_LOG_DEBUG("rxOk");
	processPacket(cbData);
}

static void rxTimeout(const dwt_cb_data_t *cbData) {
	NRF_LOG_DEBUG("rxTimeout");

	dwt_setrxtimeout(0);
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);
}

static void rxError(const dwt_cb_data_t *cbData) {
	//NRF_LOG_DEBUG("rxError");

	dwt_setrxtimeout(0);
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);
}

static void saveDistance(float meters) {
	// Increments index BEFORE saving to array, that way the last known distance is always at distanceIndex which makes it faster to grab distances when getLastDistance() is called.
	distanceIndex++;
	if (distanceIndex >= DISTANCES_COUNT) {
		distanceIndex = 0;
	}

	distanceMeters[distanceIndex] = meters;
}

// Public Function Definitions
void rxInit(void) {

	// Set delay between sending a msg and listening on RX.
	dwt_setrxaftertxdelay(TX_TO_RX_DELAY_UUS);
    //dwt_setpreambledetecttimeout(PRE_TIMEOUT);
    dwt_setpreambledetecttimeout(0);

    // Using single buffer. Had issues with double buffering skipping every other Poll.
    dwt_setdblrxbuffmode(0);

	// Set interrupt callbacks
	dwt_setcallbacks(&txDone, &rxOk, &rxTimeout, &rxError);

	// Enable wanted interrupts (RX good frames and RX errors).
    dwt_setinterrupt(DWT_INT_TFRS | DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL | DWT_INT_RFTO | DWT_INT_RXPTO | DWT_INT_SFDT | DWT_INT_ARFE, 1);

	// Set RX timeout.
	//dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
	dwt_setrxtimeout(0);
	// Start listening on RX.
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);

	NRF_LOG_DEBUG("rx Init");
}

void rxDeInit(void) {
	memset((uint8_t*)&thisSequence, 0, sizeof(thisSequence));
	isStreamingDistance = false;
}

bool processPollMsg(PacketPoll_t *packet) {
	// NRF_LOG_DEBUG("RX Poll: %i", packet->header.sequenceNumber);
	if (device != Device_CatcherGlove) {
		NRF_LOG_DEBUG("Error: invalid role");
		return false;
	}

	// Get sequence number
	memset((uint8_t*)&thisSequence, 0, sizeof(thisSequence));
	thisSequence.sequenceNumber = packet->header.sequenceNumber;

	// Get timestamp.
	dwt_readrxtimestamp((uint8_t*) &thisSequence.pollRx);
	// uint32 txDelay = (uint64_t) (thisSequence.pollRx + ((uint32_t) POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
	//uint64_t txDelay1 = 0xBEBC200;	// (200,000,000)Too low, doesn't work.
	uint64_t txDelay = 0xE484000;	// (239,616,000)Works
	//uint64_t txDelay1 = 0x1C908000;	// Works
	//uint64_t txDelay1 = 0x17CDC0000;	100 msec Works
	//uint32 txDelay2 = (uint64_t) thisSequence.pollRx + txDelay1;
	txDelay += (uint64_t) thisSequence.pollRx;
	txDelay >>= 8;

	// Passing back the same sequenceNumber.
	bool success = sendReplyMsg(packet->header.sequenceNumber, packet->header.sourceAddr, getMyAddress(), txDelay);
	if (!success) {
		resetRX();
	}
	return success;
}

bool processFinalMsg(PacketFinal_t *packet) {
	//NRF_LOG_DEBUG("RX Final");
	static uint32_t lastDistanceStreamTick = 0;

	if (device != Device_CatcherGlove) {
		return false;
	}

	bool result = false;
	if (thisSequence.sequenceNumber != packet->header.sequenceNumber) {
		NRF_LOG_DEBUG("Error: Final Msg out of Sequence");
		memset((uint8_t*)&thisSequence, 0, sizeof(thisSequence));

		dwt_setrxtimeout(0);
		rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);

		result = false;
	} else {
		// Get timestamps
		dwt_readtxtimestamp((uint8_t*) &thisSequence.replyTx);
		dwt_readrxtimestamp((uint8_t*) &thisSequence.finalRx);
		// thisSequence.replyTx = dwt_readtxtimestamplo32();
		// thisSequence.finalRx = dwt_readrxtimestamplo32();
		thisSequence.pollTx = packet->timestamps.pollTx;
		thisSequence.replyRx = packet->timestamps.replyRx;
		thisSequence.finalTx = packet->timestamps.finalTx;

		dwt_setrxtimeout(0);
		rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);

		float meters = calculateDistance(&thisSequence);
		saveDistance(meters);

		memset((uint8_t*)&thisSequence, 0, sizeof(thisSequence));

		if (isStreamingDistance) {
			// Limit send frequency
			uint32_t now = app_timer_cnt_get();
			if (app_timer_cnt_diff_compute(now, lastDistanceStreamTick) >= DISTANCE_STREAM_TICKS) {
				sendDataResponsePacket(Cmd_Distance, (uint8_t*)&meters, sizeof(meters));
				lastDistanceStreamTick = now;
			}
		}
		result = true;
	}

	return result;
}

float getLastDistance(void) {
	// Performs error checking on the distance and ignores rogue distances.
	uint8_t outerIndex = distanceIndex;
	uint8_t outerCount = 0;
	uint8_t outerMajorityCount = 0;
	uint8_t indexToUse = distanceIndex;

	while (outerCount < DISTANCES_COUNT) {
		float distanceToCompare = distanceMeters[outerIndex];

		// Ignore zero distance. This happens when a new session has started, clearDistance() is called ... and catcher glove triggers in less than 500 msecs. Therefore have a majority zeros in the distanceMeters array, which then returns zero distance even though a good distance(s) was the last written to array.
		if (distanceToCompare != 0) {
			uint8_t innerIndex = outerIndex;
			uint8_t innerCount = outerCount;
			uint8_t innerMajorityCount = 0;

			if (outerMajorityCount >= (DISTANCES_COUNT / 2)) {
				// Majority already found, no need to continue.
				break;
			}

			while (innerCount < (DISTANCES_COUNT - 1)) {
				innerIndex++;
				if (innerIndex >= DISTANCES_COUNT) {
					innerIndex = 0;
				}

				float nextDistance = distanceMeters[innerIndex];
				if (fabs(distanceToCompare - nextDistance) <= DISTANCE_TOLERANCE) {
					// Passed
					innerMajorityCount++;
					if (innerMajorityCount > outerMajorityCount) {
						outerMajorityCount = innerMajorityCount;
						indexToUse = outerIndex;
					}
				}

				innerCount++;
			}
		}

		outerCount++;
		outerIndex++;
		if (outerIndex >= DISTANCES_COUNT) {
			outerIndex = 0;
		}
	}

	// NRF_LOG_INFO("D[0]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[0]));
	// NRF_LOG_INFO("D[1]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[1]));
	// NRF_LOG_INFO("D[2]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[2]));
	// NRF_LOG_INFO("D[3]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[3]));
	// NRF_LOG_INFO("D[4]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[4]));
	// NRF_LOG_INFO("D[5]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[5]));
	// NRF_LOG_INFO("D[6]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[6]));
	// NRF_LOG_INFO("D[7]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[7]));
	// NRF_LOG_INFO("D[8]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[8]));
	// NRF_LOG_INFO("D[9]: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(distanceMeters[9]));

	float distanceToUse = distanceMeters[indexToUse];

	NRF_LOG_INFO("Distance To Use: " NRF_LOG_FLOAT_MARKER ", lastIndex: %d, indexToUse: %d", NRF_LOG_FLOAT(distanceToUse), distanceIndex, indexToUse);
	return distanceToUse;
}

void clearDistance(void) {
	distanceIndex = 0;
	memset((uint8_t*)distanceMeters, 0, sizeof(distanceMeters));
}
