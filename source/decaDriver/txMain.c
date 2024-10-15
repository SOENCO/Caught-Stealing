
#include "txMain.h"

#include "app_scheduler.h"
#include "app_timer.h"
#include "nrf_delay.h"
#include "nrf_drv_systick.h"
#include "nrf_log.h"
#include "nrf_gpio.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "popTimer.h"
#include "system.h"
#include "utility.h"

// Module Init
APP_TIMER_DEF(pollTimer_id);

// Public Variables

// Private Variables
#define POLLING_MS	50
static uint8_t sequenceNumber = 0;


// Delay between frames, in UWB microseconds. See NOTE 4 below.
// This is the delay from the end of the frame transmission to the enable of the receiver, as programmed for the DW1000's wait for response feature.
#define POLL_TX_TO_RESP_RX_DLY_UUS 300


// Private Function Declarations
static void polling(void* p_event_data, uint16_t event_size);
static TimeStamps64_t getTimestamps(void);
static void txDone(const dwt_cb_data_t *cbData);
static void rxOk(const dwt_cb_data_t *cbData);
static void rxTimeout(const dwt_cb_data_t *cbData);
static void rxError(const dwt_cb_data_t *cbData);
static void timersInit(void);
static void pollingTimer_start(void);
static void pollingTimer_stop(void);
static void pollingTimer_handler(void *context);


// Private Function Definitions
static void polling(void* p_event_data, uint16_t event_size) {
	// Sends poll msgs
	if (!isDwSleeping()) {
		//NRF_LOG_INFO("sendPoll");
		sequenceNumber++;
		sendPollMsg(sequenceNumber, getOthersRxAddress(), getMyAddress());
	}

	pollingTimer_start();
}

static TimeStamps64_t getTimestamps(void) {
	TimeStamps64_t timestamps = { .pollTx = 0, .replyRx = 0, .finalTx = 0 };
	dwt_readtxtimestamp((uint8_t*) &timestamps.pollTx);
	dwt_readrxtimestamp((uint8_t*) &timestamps.replyRx);
	//timestamps.finalTx = ((uint64_t) timestamps.replyRx + ((uint32_t) RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
	timestamps.finalTx = ((uint64_t) timestamps.replyRx + ((uint32_t) 6000 * UUS_TO_DWT_TIME)) >> 8;
	return timestamps;
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
}

static void rxError(const dwt_cb_data_t *cbData) {
	NRF_LOG_DEBUG("rxError");
}

static void timersInit(void) {
	// Init timers
	uint32_t err_code = app_timer_create(&pollTimer_id, APP_TIMER_MODE_SINGLE_SHOT, pollingTimer_handler);
	APP_ERROR_CONTINUE(err_code);
}

static void pollingTimer_start(void) {
	app_timer_stop(pollTimer_id);
	uint32_t err_code = app_timer_start(pollTimer_id, APP_TIMER_TICKS(POLLING_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void pollingTimer_stop(void) {
	app_timer_stop(pollTimer_id);
	NRF_LOG_INFO("Poll Timer stopped");
}

static void pollingTimer_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, polling);
	APP_ERROR_CONTINUE(err_code);
}


// Public Function Definitions
void txInit(void) {
	static bool initOneTime = true;

	if (initOneTime) {
		initOneTime = false;
		timersInit();
	}

    //dwt_setpreambledetecttimeout(PRE_TIMEOUT);
    dwt_setpreambledetecttimeout(0);

    // Set expected response's delay and timeout. See NOTE 4, 5 and 6 below.
    // As this example only handles one incoming frame with always the same delay and timeout, those values can be set here once for all.
    //dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    //dwt_setrxaftertxdelay(60);
    //dwt_setrxaftertxdelay(0xE484000);
    //dwt_setrxaftertxdelay(0xBEBC200);
    //dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
    //dwt_setrxtimeout(0);


    // Activate double buffering.
    dwt_setdblrxbuffmode(1);

	// Set interrupt callbacks
	dwt_setcallbacks(&txDone, &rxOk, &rxTimeout, &rxError);

	// Enable wanted interrupts
    dwt_setinterrupt(DWT_INT_TFRS | DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL | DWT_INT_RFTO | DWT_INT_RXPTO | DWT_INT_SFDT | DWT_INT_ARFE, 1);

	// Set RX timeout.
	//dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
	dwt_setrxtimeout(0);
	// Start listening on RX.
	rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);

	// Start polling
	pollingTimer_start();
}

void txDeInit(void) {
	pollingTimer_stop();
}

void processReplyMsg(PacketReply_t *packet) {
	//NRF_LOG_DEBUG("RX Reply");
	if (device != Device_FielderGlove) {
		NRF_LOG_DEBUG("Error: Not a TX Device Type");
		return;
	}

	// Validate sequence number.
	if (sequenceNumber == packet->header.sequenceNumber) {
		sendFinalMsg(sequenceNumber, getOthersRxAddress(), getMyAddress(), getTimestamps());
	} else {
		NRF_LOG_DEBUG("Error: Reply Msg out of Sequence: %i, %i", sequenceNumber, packet->header.sequenceNumber);
	}
}
