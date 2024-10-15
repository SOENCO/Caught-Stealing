#ifndef PASSKEY_H
#define PASSKEY_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>


#define PASSKEY_RIGHT_SHIFT_PERIPHERAL   3
#define PASSKEY_RIGHT_SHIFT_CENTRAL   5
#define BDADDR_SIZE		6
#define PASSKEY_SIZE	8

#define LOW_BYTE(x)		((x) & 0x000000ff)			// Extract bits 0-7 of a 32-bit number
#define HIGH_BYTE(x)	(((x) >> 8) & 0x000000ff)	// Extract bits 8-15 of a 32-bit number
#define THIRD_BYTE(x)	(((x) >> 16) & 0x000000ff)	// Extract bits 16-23 of a 32-bit number
#define FOURTH_BYTE(x)	(((x) >> 24) & 0x000000ff)	// Extract bits 24-31 of a 32-bit number

typedef enum {
	centralIsMe_ReceivedKeys,
	centralIsMe_SendingKeys,
	peripheralIsMe_ReceivedKeys,
	peripheralIsMe_SendingKeys,
} PASSKEY_HANDLE;

typedef PACKED_STRUCT {
	uint32_t key1;
	uint32_t key2;
} keys_t;

typedef PACKED_STRUCT {
	PASSKEY_HANDLE passkeyHandler;
	uint8_t *passkey;	// return ptr for generated passkey
	keys_t *keys;
	uint8_t *myAddr;	// bdAddr of this device (me)
	uint8_t *otherAddr;	// bdAddr of the other connected device (phone)
} PasskeyParameter_t;


 //Public Function Prototypes
void passkeyHandler(PasskeyParameter_t parameters);

#endif
