
#include "flashHandler.h"

#include "app_error.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "commands.h"
#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#include "nrf_log.h"
#include "utility.h"
#include "version.h"

// Public variables
Flash_t storage;

// Private variables
#define FLASH_NVM_START	0x77000		// WARNING: Stay out of bootloader and application range.
#define FLASH_NVM_END	0x77FFF

// Private Function Declarations
static void fstorageInit(void);
static void fdsEventHandler(nrf_fstorage_evt_t *p_evt);
static void readFlash(void);
static void writeFlash(void);
static ret_code_t eraseFlash(void);
static void storageInit(void);
static void convertFrom5to6(void);
static void printFlashInfo(nrf_fstorage_t *p_fstorage);
static void printStorage(void);
static uint32_t flash_end_addr(void);

// Module Init
NRF_FSTORAGE_DEF(nrf_fstorage_t fstorage) = {
        .evt_handler = fdsEventHandler,
        .start_addr = FLASH_NVM_START,
        .end_addr = FLASH_NVM_END,
};

// Private Function Definitions
static void fstorageInit(void) {
	ret_code_t err_code;

	// See if bootloader exists
	if (NRF_UICR->NRFFW[0] != 0xFFFFFFFF) {
		// Exists
    	NRF_LOG_INFO("Bootloader Start Address: %x", flash_end_addr());
	}
    NRF_LOG_INFO("Flash Start Address: %x", FLASH_NVM_START);
    NRF_LOG_INFO("Flash End Address: %x", FLASH_NVM_END);

	err_code = nrf_fstorage_init(&fstorage, &nrf_fstorage_sd, NULL);
	APP_ERROR_CONTINUE(err_code);

	printFlashInfo(&fstorage);
	while(nrf_fstorage_is_busy(&fstorage));
}

static void fdsEventHandler(nrf_fstorage_evt_t *p_evt) {
	if (p_evt->result != NRF_SUCCESS) {
		NRF_LOG_INFO("FStorage Error: %d", p_evt->result);
		return;
	}

	switch (p_evt->id) {
		case NRF_FSTORAGE_EVT_WRITE_RESULT: {
			NRF_LOG_INFO("FStorage: wrote %d bytes at address 0x%x\r\n", p_evt->len, p_evt->addr);
		} break;

		case NRF_FSTORAGE_EVT_ERASE_RESULT: {
			NRF_LOG_INFO("FStorage: erased %d page from address 0x%x\r\n",
				p_evt->len, p_evt->addr);
		} break;

		default:
			break;
	}
}

static void readFlash(void) {
	ret_code_t err_code;

	while(nrf_fstorage_is_busy(&fstorage));
	err_code = nrf_fstorage_read(&fstorage, FLASH_NVM_START, &storage, sizeof(storage));
	APP_ERROR_CONTINUE(err_code);
}

static void writeFlash(void) {
	ret_code_t err_code;

	// Copy data to a local array
	static uint32_t data[sizeof(Flash_t)];
	memset(data, 0, sizeof(Flash_t));
	memcpy(data, (uint8_t*)&storage, sizeof(Flash_t));

	while(nrf_fstorage_is_busy(&fstorage));
	if (eraseFlash() == NRF_SUCCESS) {
		while(nrf_fstorage_is_busy(&fstorage));
		err_code = nrf_fstorage_write(&fstorage, FLASH_NVM_START, &data, sizeof(data), NULL);
		APP_ERROR_CONTINUE(err_code);
	}
}

static ret_code_t eraseFlash(void) {
	ret_code_t err_code = nrf_fstorage_erase(&fstorage, FLASH_NVM_START, 1, NULL);

	if (err_code != NRF_SUCCESS) {
		NRF_LOG_INFO("Flash Erase Failed!");
	} else {
		NRF_LOG_INFO("Flash Erase Succeeded!");
	}
	return err_code;
}

static void storageInit(void) {
	// Initialize storage struct.
	memset(&storage, 0, sizeof(Flash_t));
	storage.flashVersion = VERSION_FLASH;
	storage.flashSize = sizeof(Flash_t);
	memcpy(storage.fullname, NO_CUSTOM_NAME, sizeof(NO_CUSTOM_NAME));
	storage.sessionTimeout = DEFAULT_SESSION_TIMEOUT_SECONDS;
	storage.groupId = UNGROUPED_ID;
	storage.catcherGloveFICR = 0;
	storage.catcherWristFICR = 0;
	storage.fielderGloveFICR = 0;
	storage.ownerId = UNOWNED_ID;
}

static void convertFrom5to6(void) {
	storage.flashVersion = VERSION_FLASH;
	storage.flashSize = sizeof(Flash_t);
	storage.ownerId = UNOWNED_ID;
    NRF_LOG_INFO("Flash: Converted from version 5 to 6");
}

static uint32_t flash_end_addr(void) {
    uint32_t const bootloader_addr = NRF_UICR->NRFFW[0];
    uint32_t const page_sz = NRF_FICR->CODEPAGESIZE;
	#ifndef NRF52810_XXAA
		uint32_t const code_sz = NRF_FICR->CODESIZE;
	#else
		// Number of flash pages, necessary to emulate the NRF52810 on NRF52832.
		uint32_t const code_sz = 48;
	#endif

    return (bootloader_addr != 0xFFFFFFFF) ? bootloader_addr : (code_sz * page_sz);
}

static void printFlashInfo(nrf_fstorage_t *p_fstorage) {
	NRF_LOG_INFO("========| flash info |========");
	NRF_LOG_INFO("erase unit: \t%d bytes", p_fstorage->p_flash_info->erase_unit);
	NRF_LOG_INFO("program unit: \t%d bytes", p_fstorage->p_flash_info->program_unit);
	NRF_LOG_INFO("==============================");
}

static void printStorage(void) {
	NRF_LOG_INFO("");
	NRF_LOG_INFO("storage.flashVersion\t\t:%d", storage.flashVersion);
	NRF_LOG_INFO("storage.flashSize\t\t\t:%d", storage.flashSize);
	NRF_LOG_INFO("storage.fullname\t\t\t:%s", storage.fullname);
	NRF_LOG_INFO("storage.sessionTimeout\t\t:%d", storage.sessionTimeout);
	NRF_LOG_INFO("storage.groupId\t\t\t:%d", storage.groupId);
	NRF_LOG_INFO("storage.catcherGloveFICR\t\t:%llu", storage.catcherGloveFICR);
	NRF_LOG_INFO("storage.catcherWristFICR\t\t:%llu", storage.catcherWristFICR);
	NRF_LOG_INFO("storage.fielderGloveFICR\t\t:%llu", storage.fielderGloveFICR);
	NRF_LOG_INFO("storage.ownerId\t\t\t:%llu", storage.ownerId);
}

// Public Function Definitions

void flashInit(void) {
	fstorageInit();

	// Get flash memory and print
	readFlash();
	printStorage();

	// See if memory initialized
	if (storage.flashVersion != VERSION_FLASH || storage.flashSize != sizeof(storage)) {

		if (storage.flashVersion == 5) {
			convertFrom5to6();
		} else {
			// Wipe and start over
			storageInit();
		}

		writeFlash();
		printStorage();
	}
}

void updateFlash(void) {
	NRF_LOG_INFO("Updating Flash!!");
	writeFlash();
}
