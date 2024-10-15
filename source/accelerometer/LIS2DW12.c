
#include "LIS2DW12.h"

#include "app_scheduler.h"
#include "commands.h"
#include "spiHandler.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "gpio.h"
#include "popTimer.h"
#include "system.h"
#include "utility.h"



// Module Init
APP_TIMER_DEF(noAccelActivity_timerId);
APP_TIMER_DEF(shaken_timerId);

// Public Variables
bool isShaken = false;

// Private Variables
#define ACCEL_WHO_AM_I							0x44	// Decimal 68
#define GET_XYZ_TICKS							APP_TIMER_TICKS(5)
#define ACCEL_SLEEP_ENABLED						0x40
#define ACCEL_SLEEP_DISABLED					0x00
#define ACCEL_WAKE_THRESHOLD					1	// 6-bit, unsigned 1 LSB = 1/64 of FS, datasheet page 48
#define ACCEL_NO_ACCEL_ACTIVITY_MONITOR_SECONDS	10
#define ACCEL_NO_ACCEL_ACTIVITY_TRIGGER_SECONDS		(3 * 60)	// 3 minutes
static uint16_t noAccelActivityDurationSeconds = 0;
static bool foundActivity = false;
static XYZ_t lastXYZ = { .x = 0, .y = 0, .z = 0 };

#define ACCEL_SHAKEN_SECONDS				15


// Private Function Declarations
static void chipInit(void);
static void streamMonitor(void);
static void spiRead(uint8_t address, uint8_t *value, uint8_t size);
static void spiWrite(uint8_t address, uint8_t *value, uint8_t size);
static void chipSelectLow(void);
static void chipSelectHigh(void);
static void inAccelActivityMonitor(void);

// Timers
static void timersInit(void);
static void noAccelActivity_start(void);
static void noAccelActivity_stop(void);
static void noAccelActivity_handler(void *context);
static void noAccelActivity_handler_AppContext(void* p_event_data, uint16_t event_size);

static void shaken_start(void);
static void shaken_stop(void);
static void shaken_handler(void *context);
static void shaken_handler_AppContext(void* p_event_data, uint16_t event_size);

// Private Function Definitions
static void chipInit(void) {
	chipSelectHigh();

	// 4-wire interface, SPI & I2C enabled, Register address auto-incremented (enabled), Block data update, CS: no Pullup, Soft-Reset Enabled, Boot disabled
	uint8_t data = 0x4C;
	spiWrite(ADDR_CTRL2, &data, 1);

	data = 0x00;
	spiRead(ADDR_WHO_AM_I, &data, 1);
	NRF_LOG_INFO("Accel WhoAmI: %i", data);

	// Validate WhoAmI
	if (data != ACCEL_WHO_AM_I) {
		NRF_LOG_INFO("Accel WhoAmI Failed");
		hardwareError();
	}

	// ODR[3:0] = 0b0101 High-Performance/Low-Power mode 100 Hz
	// MODE[1:0] = 0b00 Low-Power Mode (12/14-bit resolution)
	// LP_MODE[1:0] = 0b10 Low-Power Mode 3 (14-bit resolution)
	//data = 0x52;	// 100 Hz, Low-Power Mode, Mode 3
	//data = 0x56;	// 100 Hz, High-Performance Mode, Mode 3
	data = 0x62;	// 200 Hz, Low-Power Mode, Mode 3
	spiWrite(ADDR_CTRL1, &data, 1);

	// Wakeup recognition -> INT1
	data = 0x20;
	spiWrite(ADDR_CTRL4_INT1_PAD_CTRL, &data, 1);

	// Low Noise disabled, Low-pass filter selected, +-8g, ODR/2
	data = 0x20;
	spiWrite(ADDR_CTRL6, &data, 1);

	// Interrupts Enabled, Data Ready pulse latched
	data = 0x20;
	spiWrite(ADDR_CTRL7, &data, 1);

	// SLEEP_ON = enabled, Wakeup Threshold
	accelSetSleep(false);

	// Sleep Duration: (16*(1/ODR)), STATIONARY = disabled, Wakeup Duration = (3* 1/ODR)
	data = 0x60;
	spiWrite(ADDR_WAKE_UP_DUR, &data, 1);
}

static void streamMonitor(void) {
	if (!streamAccelerometer) {
		return;
	}

	// Limit frequency of timestamp checks
	static uint32_t lastTick = 0;
	uint32_t now = app_timer_cnt_get();
	if (app_timer_cnt_diff_compute(now, lastTick) <= GET_XYZ_TICKS) {
 		return;
 	}
	lastTick = now;

	XYZ_t xyz = getXYZ();
	sendDataResponsePacket(Cmd_Accelerometer, (uint8_t*)&xyz, sizeof(xyz));
	NRF_LOG_INFO("x: %i, y: %i, z: %i", xyz.x, xyz.y, xyz.z);
}

static void spiRead(uint8_t address, uint8_t *value, uint8_t size) {
	static uint8_t txBytes[SPI_READ_BYTES_MAX];
	static uint8_t rxBytes[SPI_READ_BYTES_MAX];
	uint8_t totalBytes = 1 + size; // (address | SPI_READ_CMD) + size

	if (totalBytes > SPI_READ_BYTES_MAX) {
		NRF_LOG_INFO("%s: Error, too many bytes: %d\r\n", __FUNCTION__, totalBytes);
		return;
	}

	memset(txBytes, SPI_EMPTY, sizeof(txBytes));
	memset(rxBytes, 0, sizeof(rxBytes));

	txBytes[0] = address | SPI_READ_CMD;

	chipSelectLow();
	spiReadArray(SPI_LIS2DW12, txBytes, rxBytes, totalBytes, NRF_DRV_SPI_MODE_0);
	chipSelectHigh();

	// Pull rx values out
	memcpy(value, &rxBytes[1], size);
}

static void spiWrite(uint8_t address, uint8_t *value, uint8_t size) {
	static uint8_t txBytes[SPI_READ_BYTES_MAX];

	memset(txBytes, SPI_EMPTY, sizeof(txBytes));
	txBytes[0] = address;	// | SPI_WRITE_CMD;
	memcpy(&txBytes[1], value, size);

	chipSelectLow();
	spiWriteBytes(SPI_LIS2DW12, txBytes, (size + 1), NRF_DRV_SPI_MODE_0);
	chipSelectHigh();
}

static void chipSelectLow(void) {
	pinClear(PIN_ACCEL_CS);
	nrf_delay_ms(1);	// 12 nsec minimum before SCLK.
}

static void chipSelectHigh(void) {
	nrf_delay_ms(1);	// 15 nsec minimum
	pinSet(PIN_ACCEL_CS);
	nrf_delay_ms(1);
}

static void inAccelActivityMonitor(void) {
	// Watches for activity.
	if (nrf_gpio_pin_read(PIN_ACCEL_INT1)) {
		foundActivity = true;
		isShaken = true;
		shaken_start();
	}
}


// Timers
static void timersInit(void) {
	// Init timers
	uint32_t err_code = app_timer_create(&noAccelActivity_timerId, APP_TIMER_MODE_REPEATED, noAccelActivity_handler);
	APP_ERROR_CONTINUE(err_code);

	err_code = app_timer_create(&shaken_timerId, APP_TIMER_MODE_SINGLE_SHOT, shaken_handler);
	APP_ERROR_CONTINUE(err_code);

	noAccelActivity_start();
}

static void noAccelActivity_start(void) {
	app_timer_stop(noAccelActivity_timerId);
	uint32_t err_code = app_timer_start(noAccelActivity_timerId, APP_TIMER_TICKS(ACCEL_NO_ACCEL_ACTIVITY_MONITOR_SECONDS * 1000), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void noAccelActivity_stop(void) {
	app_timer_stop(noAccelActivity_timerId);
}

static void noAccelActivity_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, noAccelActivity_handler_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void noAccelActivity_handler_AppContext(void* p_event_data, uint16_t event_size) {
	// In App Context
	if (foundActivity) {
		foundActivity = false;
		noAccelActivityDurationSeconds = 0;
		//debugPrint("Found Motion: Reset Wake");
	} else {
		noAccelActivityDurationSeconds += ACCEL_NO_ACCEL_ACTIVITY_MONITOR_SECONDS;
		//debugPrint("Duration: %i", noAccelActivityDurationSeconds);
		if (noAccelActivityDurationSeconds >= ACCEL_NO_ACCEL_ACTIVITY_TRIGGER_SECONDS) {
			// Go to sleep
			debugPrint("No Motion for %i seconds", ACCEL_NO_ACCEL_ACTIVITY_TRIGGER_SECONDS);
			systemSleep(Sleep_All);
		}
	}
}

static void shaken_start(void) {
	app_timer_stop(shaken_timerId);
	uint32_t err_code = app_timer_start(shaken_timerId, APP_TIMER_TICKS(ACCEL_SHAKEN_SECONDS * 1000), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void shaken_stop(void) {
	app_timer_stop(shaken_timerId);
}

static void shaken_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, shaken_handler_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void shaken_handler_AppContext(void* p_event_data, uint16_t event_size) {
	// In App Context
	isShaken = false;
}


// Public Function Definitions
uint8_t accelInit(void) {
	timersInit();
	chipInit();
}

void accelLoop(void) {
	inAccelActivityMonitor();
	streamMonitor();
}

void readXYZ(void) {
	spiRead(ADDR_OUT_X_L, (uint8_t*)&lastXYZ, sizeof(XYZ_t));
}

int16_t getAxis(Axis_t axis, XYZ_t xyz) {
	switch (axis) {
		case Axis_X: return xyz.x;
		case Axis_Y: return xyz.y;
		case Axis_Z: return xyz.z;
		default: return 0;
	}
}

void accelSetSleep(bool sleep) {
	// Enable/disable sleep
	uint8_t enable = (sleep) ? ACCEL_SLEEP_ENABLED : ACCEL_SLEEP_DISABLED;
	uint8_t data = enable | ACCEL_WAKE_THRESHOLD;
	spiWrite(ADDR_WAKE_UP_THS, &data, 1);
	debugPrint("Accel: %s", ((sleep) ? "Sleep\n\n" : "Wake"));
}

XYZ_t getXYZ(void) {
	return lastXYZ;
}
