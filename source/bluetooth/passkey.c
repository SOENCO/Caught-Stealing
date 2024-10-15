
#include "passkey.h"

#include "utility.h"
#include <nrf_delay.h>
#include <nrf_drv_rng.h>
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#define MAX_EMBEDDED_VALUE	4
#define RANDOM_MAX 32767

// Private Variables
uint8_t phone_embedded_value[MAX_EMBEDDED_VALUE] = {0x51, 0x6C, 0x5A, 0x19};
uint8_t device_embedded_value[MAX_EMBEDDED_VALUE] = {0x3F, 0xCA, 0x19, 0x6D};


// Private Function Declarations
static uint8_t random_vector_generate(uint8_t * p_buff, uint8_t size);
static void getPassKey_Device(uint8_t *passkey, keys_t *keys, uint8_t *bdAddr1, uint8_t *bdAddr2, uint8_t *embed_val, uint8_t shift_val);
static void getPassKey_Phone(uint8_t *passkey, keys_t *keys, uint8_t *bdAddr1, uint8_t *bdAddr2, uint8_t *embed_val, uint8_t shift_val);
static void combineTwoUint(uint32_t data1, uint32_t data2, uint8_t *data);


// Private Function Definitions
static uint8_t random_vector_generate(uint8_t * p_buff, uint8_t size) {
	uint8_t available;
	nrf_drv_rng_bytes_available(&available);
	uint8_t length = MIN(size, available);

	uint32_t err_code = nrf_drv_rng_rand(p_buff, length);
	APP_ERROR_CONTINUE(err_code);

	return length;
}

static void getPassKey_Device(uint8_t *passkey, keys_t *keys, uint8_t *bdAddr1, uint8_t *bdAddr2, uint8_t *embed_val, uint8_t shift_val) {
	// This function writes back values on 'passkey' and 'keys'. 'keys' is written back to only if key1 = 0.
	uint16_t count = 0;
	uint32_t hash1 = 0;
	uint32_t hash2 = 0;

	if (embed_val == device_embedded_value) {
		if (!keys->key1) {
			while (random_vector_generate((uint8_t *)&keys->key1, sizeof(keys->key1)) < sizeof(keys->key1)) {
				nrf_delay_ms(100);
			}
			keys->key2 = 0;
		}

		uint32_t rand_num;
		while (random_vector_generate((uint8_t *)&rand_num, sizeof(rand_num)) < sizeof(rand_num)) {
			nrf_delay_ms(100);
		}
		keys->key2 += (rand_num / (RANDOM_MAX / 11)) + 1;
	}

	hash1 = (FOURTH_BYTE(keys->key1) << 24) |
		(HIGH_BYTE(keys->key1) << 16) |
		(FOURTH_BYTE(keys->key2) << 8) |
		HIGH_BYTE(keys->key2);

	hash2 = (THIRD_BYTE(keys->key1) << 24) |
		(LOW_BYTE(keys->key1) << 16) |
		(THIRD_BYTE(keys->key2) << 8) |
		LOW_BYTE(keys->key2);

	for(count = 0; count < 2; count++)	 {
		hash1 += (uint32_t)(bdAddr1[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 4; count < 6; count++) {
		hash2 += (uint32_t)(bdAddr1[count]);
		hash2 ^= (hash2 << shift_val);
	}

	for(count = 2; count < 4; count++)	 {
		hash1 += (uint32_t)(bdAddr2[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 4; count < 6; count++)	 {
		hash2 += (uint32_t)(bdAddr2[count]);
		hash2 ^= (hash2 << shift_val);
	}

	for(count = 0; count < 2; count++) {
		hash1 += (uint32_t)(embed_val[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 2; count < 4; count++) {
		hash2 += (uint32_t)(embed_val[count]);
		hash2 ^= (hash2 << shift_val);
	}

	combineTwoUint(hash1, hash2, passkey);
}

static void getPassKey_Phone(uint8_t *passkey, keys_t *keys, uint8_t *bdAddr1, uint8_t *bdAddr2, uint8_t *embed_val, uint8_t shift_val) {
	// This function writes back values on 'passkey' and 'keys'. 'keys' is written back to only if key1 = 0.
	uint16_t count = 0;
	uint32_t hash1 = 0;
	uint32_t hash2 = 0;

	if (!keys->key2) {
		while (random_vector_generate((uint8_t *)&keys->key1, sizeof(keys->key1)) < sizeof(keys->key1)) {
			nrf_delay_ms(100);
		}

		keys->key2 = 0;
	}

	uint32_t rand_num;

	if (!keys->key2) {
		while (random_vector_generate((uint8_t *)&rand_num, sizeof(rand_num)) < sizeof(rand_num)) {
			nrf_delay_ms(100);
		}

		keys->key2 += (rand_num / (RANDOM_MAX / 11)) + 1;
	}

	hash1 = (FOURTH_BYTE(keys->key1) << 24) |
		(HIGH_BYTE(keys->key1) << 16) |
		(FOURTH_BYTE(keys->key2) << 8) |
		HIGH_BYTE(keys->key2);

	hash2 = (THIRD_BYTE(keys->key1) << 24) |
		(LOW_BYTE(keys->key1) << 16) |
		(THIRD_BYTE(keys->key2) << 8) |
		LOW_BYTE(keys->key2);

	for(count = 0; count < 2; count++) {
		hash1 += (uint32_t)(bdAddr1[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 4; count < 6; count++) {
		hash2 += (uint32_t)(bdAddr1[count]);
		hash2 ^= (hash2 << shift_val);
	}

	for(count = 2; count < 4; count++) {
		hash1 += (uint32_t)(bdAddr2[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 4; count < 6; count++) {
		hash2 += (uint32_t)(bdAddr2[count]);
		hash2 ^= (hash2 << shift_val);
	}

	for(count = 0; count < 2; count++) {
		hash1 += (uint32_t)(embed_val[count]);
		hash1 ^= (hash1 << shift_val);
	}

	for(count = 2; count < 4; count++) {
		hash2 += (uint32_t)(embed_val[count]);
		hash2 ^= (hash2 << shift_val);
	}

	combineTwoUint(hash1, hash2, passkey);
}

static void combineTwoUint(uint32_t data1, uint32_t data2, uint8_t *data) {
	// This function concatenates the two unit32 values
	data[0] = FOURTH_BYTE(data1);
	data[1] = THIRD_BYTE(data1);
	data[2] = HIGH_BYTE(data1);
	data[3] = LOW_BYTE(data1);
	data[4] = FOURTH_BYTE(data2);
	data[5] = THIRD_BYTE(data2);
	data[6] = HIGH_BYTE(data2);
	data[7] = LOW_BYTE(data2);
}


// Public Function Definitions
void passkeyHandler(PasskeyParameter_t parameters) {
	switch (parameters.passkeyHandler) {
		case centralIsMe_ReceivedKeys:
			getPassKey_Phone(parameters.passkey, parameters.keys, parameters.otherAddr, parameters.myAddr, device_embedded_value, PASSKEY_RIGHT_SHIFT_PERIPHERAL);
			break;
		case centralIsMe_SendingKeys:
			getPassKey_Phone(parameters.passkey, parameters.keys, parameters.myAddr, parameters.otherAddr, phone_embedded_value, PASSKEY_RIGHT_SHIFT_CENTRAL);
			break;
		case peripheralIsMe_ReceivedKeys:
			getPassKey_Device(parameters.passkey, parameters.keys, parameters.otherAddr, parameters.myAddr, phone_embedded_value, PASSKEY_RIGHT_SHIFT_CENTRAL);
			break;
		case peripheralIsMe_SendingKeys:
			getPassKey_Device(parameters.passkey, parameters.keys, parameters.myAddr, parameters.otherAddr, device_embedded_value, PASSKEY_RIGHT_SHIFT_PERIPHERAL);
			break;

		default:
			break;
	}
}
