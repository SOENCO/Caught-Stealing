
#include "dwCommon.h"

#include "app_scheduler.h"
#include "commands.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "flashHandler.h"
#include "gpio.h"
#include "popTimer.h"
#include "rxMain.h"
#include "spiHandler.h"
#include "system.h"
#include "txMain.h"
#include "utility.h"

// Module Init
APP_TIMER_DEF(statMonitorTimer_id);

// Public Variables
dwt_config_t config = {
    5,               /* Channel number. */
    DWT_PRF_64M,     /* Pulse repetition frequency. */
    DWT_PLEN_1024,   /* Preamble length. Used in TX only. */
    DWT_PAC32,       /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    1,               /* 0 to use standard SFD, 1 to use non-standard SFD. */
    DWT_BR_110K,     /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    (1024 + 1 + DW_NS_SFD_LEN_110K - 32) /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
};

//For Channel 2 operation
//dwt_txconfig_t txConfig = {
//    .PGdly = 0xC2,
//    .power = 0x67676767
//};

//For Channel 5 operation
dwt_txconfig_t txConfig = {
	.PGdly = 0xB5,
	.power = 0x85858585,
};

bool isStreamingDistance = false;
bool streamAccelerometer = false;

// Private Variables
#define SYS_STAT_FREQ_TICKS		APP_TIMER_TICKS(100)
static PmscState_t desiredPmsc = PmscState_Init;
static uint16_t desiredPmscCounter = 0;

// Private Function Declarations
static void initializeDW1000(void);
static bool dw1000Reset(void);
static void setAntennaDelay(void);
static PacketGeneric_t* readPacket(const dwt_cb_data_t *cbData);
static bool isValidPacket(PacketGeneric_t *packet, uint16 size);
static void readSysStat(void* p_event_data, uint16_t event_size);
static void timersInit(void);
static void statMonitorTimer_start(void);
static void statMonitorTimer_stop(void);
static void statMonitorTimer_handler(void *context);


// Private Function Definitions
static void initializeDW1000(void) {
	// Initializes DW1000 chip.

	// Ensure DW is awake by toggling CS line.
	pinClear(PIN_DWM_SPI_CS);
	nrf_delay_ms(1);			// NOTE: Errata increased the delay from 500 usec to 850 usec.
	pinSet(PIN_DWM_SPI_CS);
	nrf_delay_ms(1);

	// Ensure slow-rate SPI. Must be <3 MHz before calling dwt_initialise(), according to DW1000_Software_API_Guide_rev2p7.pdf page 20.
	spiSetFrequency(SPI_DECAWAVE, NRF_SPI_FREQ_2M);
	nrf_delay_ms(1);

	if (!dw1000Reset()) {
		wristOnly();
		NRF_LOG_DEBUG("Assuming Wrist-only device.");
		return;
	}

	// SPI handler defaults to <3 MHz
	if (dwt_initialise(DWT_LOADUCODE) == DWT_ERROR) {
		NRF_LOG_INFO("INIT FAILED");
	}
	// Switch to high-rate SPI
	spiSetFrequency(SPI_DECAWAVE, NRF_SPI_FREQ_8M);
    nrf_delay_ms(1);

	// Send configuration settings to DW.
	dwt_forcetrxoff(); // Ensure in idle mode before calling configure.
    dwt_configure(&config);
	setAntennaDelay();

	// Enable EXTRXE (external receiver enable, GPIO6) & EXTTXE (external transmit enable, GPIO5) for debugging TX & RX states on oscilliscope.
	dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

	// Configure sleep
	dwt_configuresleep(DWT_PRESRV_SLEEP | DWT_CONFIG, DWT_WAKE_CS | DWT_SLP_EN);
}

static bool dw1000Reset(void) {
	// Attempts to reset DW, return false if failed.

	// Ensure RST set as an output.
	nrf_gpio_cfg_output(PIN_DWM_RST);
    pinClear(PIN_DWM_RST);
    nrf_delay_us(1);

	// NEVER drive PIN_DWM_RST high, according to DWM100 Datasheet, page 5. Instead set to high-impedance (input) to release from reset.
    nrf_gpio_cfg_input(PIN_DWM_RST, NRF_GPIO_PIN_NOPULL);
    nrf_delay_ms(2);

	// Wait til pin is pulled high by DW's internal circuit.
	int16_t timeout = 100;
	while (!nrf_gpio_pin_read(PIN_DWM_RST) && (timeout > 0)) {
    	nrf_delay_ms(1);
		timeout--;
		if (timeout <= 0) {
			NRF_LOG_DEBUG("DW1000 didn't pull REST high.");
			return false;
		}
	}

    nrf_delay_ms(10);
	return true;
}

static void setAntennaDelay(void) {
	//dwt_configuretxrf(&txConfig);	// This sets TX Power
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);
}

static PacketGeneric_t* readPacket(const dwt_cb_data_t *cbData) {
	// Reads packet and validates. Returns pointer to packet if success.
	static PacketGeneric_t packet;

	uint16 size = cbData->datalength;
	if (size < PACKET_SIZE_MIN) {
		NRF_LOG_DEBUG("Packet Error: (size < PACKET_SIZE_MIN)");
		return NULL;
	} else if (size > sizeof(PacketGeneric_t)) {
		NRF_LOG_DEBUG("Packet Error: (size > sizeof(PacketGeneric_t))");
		return NULL;
	}

	memset((uint8_t*)&packet, 0, sizeof(packet));
	dwt_readrxdata((uint8_t*)&packet, size, 0);

	// NRF_LOG_DEBUG("Packet size = %d", size);
	// NRF_LOG_INFO_ARRAY((uint8_t*)&packet, size);

	if (!isValidPacket(&packet, size)) {
		return NULL;
	}

	return &packet;
}

static bool isValidPacket(PacketGeneric_t *packet, uint16 size) {

	// Validate header.
	bool isHeaderValid = (packet->header.frameControl == FRAME_CONTROL) &&
						 (packet->header.panId == PAN_ID) &&
						 (packet->header.destinationAddr == getMyAddress());
	if (!isHeaderValid) {
		NRF_LOG_DEBUG("Packet Error: isHeaderValid");
		NRF_LOG_DEBUG("frameControl: 0x%x", packet->header.frameControl);
		NRF_LOG_DEBUG("panId: 0x%x", packet->header.panId);
		NRF_LOG_DEBUG("destinationAddr: 0x%x", packet->header.destinationAddr);
		NRF_LOG_DEBUG("My Address: 0x%x", getMyAddress());
		return false;
	}

	// Validate msg type and size.
	bool isSizeValid = false;
	switch (packet->header.msgType) {
		case MsgType_poll:
			isSizeValid = (size == sizeof(PacketPoll_t));
			break;
		case MsgType_reply:
			isSizeValid = (size == sizeof(PacketReply_t));
			break;
		case MsgType_final:
			isSizeValid = (size == sizeof(PacketFinal_t));
			break;
		default:
			NRF_LOG_DEBUG("Packet Error: MsgType unknown");
			return false;
	}

	if (!isSizeValid) {
		NRF_LOG_DEBUG("Packet Error: Invalid Size");
	}
	return isSizeValid;
}

static void readSysStat(void* p_event_data, uint16_t event_size) {
	SystemState_t sysState = {0};
	dwt_readfromdevice(SYS_STATE_ID, 0, sizeof(sysState), (uint8_t*)&sysState);

	// Ensure in the desired state
	switch (desiredPmsc) {
		case PmscState_Init:
		case PmscState_Idle:
		case PmscState_TxWait:
		case PmscState_Tx:
			desiredPmscCounter = 0;
			break;
		case PmscState_RxWait:
		case PmscState_Rx:
			if (!((sysState.pmscState == PmscState_RxWait) || (sysState.pmscState == PmscState_Rx))) {
				desiredPmscCounter++;
			}
			if (desiredPmscCounter >= 4) {
				desiredPmscCounter = 0;
				// NRF_LOG_INFO("PMSC = %u, Restarting RX", sysState.pmscState);
				rxEnable(DWT_START_RX_IMMEDIATE | DWT_NO_SYNC_PTRS);
			}
			break;
		default:
			break;
	}
}

static void timersInit(void) {
	// Init timers
	uint32_t err_code = app_timer_create(&statMonitorTimer_id, APP_TIMER_MODE_REPEATED, statMonitorTimer_handler);
	APP_ERROR_CONTINUE(err_code);
}

static void statMonitorTimer_start(void) {
	app_timer_stop(statMonitorTimer_id);
	uint32_t err_code = app_timer_start(statMonitorTimer_id, SYS_STAT_FREQ_TICKS, NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void statMonitorTimer_stop(void) {
	app_timer_stop(statMonitorTimer_id);
}

static void statMonitorTimer_handler(void *context) {
	// Move to App Context
	if (!isDwSleeping()){
		ret_code_t err_code = app_sched_event_put(NULL, 0, readSysStat);
		APP_ERROR_CONTINUE(err_code);
	}
}


// Public Function Definitions
void dwInit(void) {
	timersInit();
	initializeDW1000();
	statMonitorTimer_start();
}

void dwServiceInterrupt(void) {
	// Monitor DWT pin interrupts

	// Catcher Wrist device doesn't have DW component
	if (device == Device_CatcherWrist) {
		return;
	}

	while (nrf_gpio_pin_read(PIN_DWM_INT)) {
		//NRF_LOG_RAW_INFO("*");
		dwt_isr();
	}
}

bool sendPollMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr) {
	// Creates and sends Poll msg. Return true if successfully started.

	PacketPoll_t msg = {
		.header.frameControl = FRAME_CONTROL,
		.header.sequenceNumber = sequenceNumber,
		.header.panId = PAN_ID,
		.header.destinationAddr = destinationAddr,
		.header.sourceAddr = sourceAddr,
		.header.msgType = MsgType_poll,
		.checksum = 0,
	};

	dwt_setdelayedtrxtime(0);

 	// Copy msg to TX buffer.
	dwt_writetxdata(sizeof(msg), (uint8_t*)&msg, 0);

	// Zero offset in TX buffer, ranging.
	dwt_writetxfctrl(sizeof(msg), 0, 1);

	//dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
	dwt_setrxtimeout(0);

	//NRF_LOG_DEBUG("TX Poll: %i", sequenceNumber);
	// Start transmission, indicating that a response is expected so that reception is enabled set by dwt_setrxaftertxdelay() has elapsed.
	//bool success = (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) == DWT_SUCCESS);
	if (txStart(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
		NRF_LOG_DEBUG("Error: Sending Poll Msg");
		return false;
	}
	return true;
}

bool sendReplyMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr, uint32_t txDelay) {
	// Creates and sends Reply msg. Return true if successfully started.
	PacketReply_t msg = {
		.header.frameControl = FRAME_CONTROL,
		.header.sequenceNumber = sequenceNumber,
		.header.panId = PAN_ID,
		.header.destinationAddr = destinationAddr,
		.header.sourceAddr = sourceAddr,
		.header.msgType = MsgType_reply,
		.activityCode = ActivityCode_ContinueRanging,
		.activityParameter = 0,
		.checksum = 0,
	};

	// Set send time for response.
	dwt_setdelayedtrxtime(txDelay);

	// Set expected delay and timeout for final message reception. See NOTE 4 and 5 below.
	dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);

 	// Copy msg to TX buffer.
	dwt_writetxdata(sizeof(msg), (uint8_t*)&msg, 0);

	// Zero offset in TX buffer, ranging.
	dwt_writetxfctrl(sizeof(msg), 0, 1);

	// Set RX timeout, which triggers SYS_STATUS_ALL_RX_TO.
	dwt_setrxtimeout(FINAL_RX_TIMEOUT_UUS);

	//NRF_LOG_DEBUG("TX Reply: %i", msg.header.sequenceNumber);

	if (txStart(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
		// NRF_LOG_DEBUG("Error: Sending Reply Msg");
		return false;
	}
	return true;
}

void sendFinalMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr, TimeStamps64_t timestamps) {

	// Final TX timestamp is transmission time we programmed plus the TX antenna delay.
	uint64_t finalTxPlusAntenna = (uint64_t) (((uint64_t) (timestamps.finalTx & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

	PacketFinal_t msg = {
		.header.frameControl = FRAME_CONTROL,
		.header.sequenceNumber = sequenceNumber,
		.header.panId = PAN_ID,
		.header.destinationAddr = destinationAddr,
		.header.sourceAddr = sourceAddr,
		.header.msgType = MsgType_final,
		.timestamps.pollTx = (timestamps.pollTx & 0xFFFFFFFF),
		.timestamps.replyRx = (timestamps.replyRx & 0xFFFFFFFF),
		.timestamps.finalTx = (finalTxPlusAntenna & 0xFFFFFFFF),
		.checksum = 0,
	};

	dwt_setdelayedtrxtime(timestamps.finalTx);

	// Send message.
 	// Zero offset in TX buffer.
	dwt_writetxdata(sizeof(msg), (uint8_t*)&msg, 0);

	// Zero offset in TX buffer, ranging.
	dwt_writetxfctrl(sizeof(msg), 0, 1);

	//NRF_LOG_DEBUG("TX Final");
	if (txStart(DWT_START_TX_DELAYED) != DWT_SUCCESS) {
		NRF_LOG_DEBUG("Error: Sending Final Msg");
	}
}

void processPacket(const dwt_cb_data_t *cbData) {
	// Reads, validates and processes packet.
	PacketGeneric_t *packet = NULL;

	// Read & Validate packet
	packet = readPacket(cbData);
	if (packet == NULL) {
		return;
	}

	switch (packet->header.msgType) {
		case MsgType_poll:
			processPollMsg((PacketPoll_t*) packet);
			break;
		case MsgType_reply:
			processReplyMsg((PacketReply_t*) packet);
			break;
		case MsgType_final:
			processFinalMsg((PacketFinal_t*) packet);
			break;
		default:
			NRF_LOG_DEBUG("MsgType_invalid");
			break;
	}
}

void clearErrors(void) {
	// NRF_LOG_DEBUG("Clear RX Error/Timeout");
	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);

	// Reset RX to properly reinitialize LDE operation.
	// TODO: dwt_rxreset();
}

uint16_t getMyAddress(void) {
	// Last 2 bytes of FICR
	return (uint16_t) (ficrID & 0xFFFF);
}

uint16_t getOthersRxAddress(void) {
	// Last 2 bytes of Catcher's Glove FICR
	return (uint16_t) (storage.catcherGloveFICR & 0xFFFF);
}

bool rxEnable(int mode) {
	// Start listening on RX.
	desiredPmscCounter = 0;
	desiredPmsc = (mode & DWT_START_RX_DELAYED) ? PmscState_RxWait : PmscState_Rx;

	// Can't switch to RX if not in idle, ie can't be in TX mode and switch to RX.
	dwt_forcetrxoff();

	bool success = dwt_rxenable(mode) == DWT_SUCCESS;
	if (success) {
		//NRF_LOG_DEBUG("RX enabled");
	} else {
		NRF_LOG_DEBUG("Error: RX enable");
	}
	return success;
}

bool txStart(int mode) {
	// Start TX.
	desiredPmscCounter = 0;
	desiredPmsc = (mode & DWT_START_TX_DELAYED) ? PmscState_TxWait : PmscState_Tx;

	// Can't switch to TX if not in IDLE, ie can't be in RX mode and switch to TX.
	dwt_forcetrxoff();

	bool result = dwt_starttx(mode);
	if (result == DWT_SUCCESS) {
		//NRF_LOG_DEBUG("TX start");
	} else {
		// NRF_LOG_DEBUG("Error: TX start");
	}
	return result;
}

void dwSetSleep(bool sleep) {
	// Wakes or sleep.

	if (device == Device_CatcherWrist) {
		return;
	}

	if (sleep) {
		NRF_LOG_DEBUG("DW Sleep");
		dwt_entersleep();
	} else {
		NRF_LOG_DEBUG("DW Wake");

		// Initialize from beginning.
		initializeDW1000();
		if (device == Device_CatcherGlove) {
			rxInit();
		} else if (device == Device_FielderGlove) {
			txInit();
		}
	}
}
