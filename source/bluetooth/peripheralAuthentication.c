
#include "peripheralAuthentication.h"

#include "bleInitialize.h"
#include "passkey.h"
#include "peripheralHandler.h"

// Private Variables
static keys_t peripheralLastKeysSent = { .key1 = 0, .key2 = 0 };


// Private Function Declarations
static void peripheralAuth_step3(bool isSuccess);


// Function Definitions
void peripheralAuth_step1(void) {
	// Write devicePasskey to GATT and notify change.
	debugPrint("Auth Step 1");

	uint8_t passkey[PASSKEY_SIZE];
	keys_t keys = { .key1 = 0, .key2 = 0 };

	PasskeyParameter_t passkeyParameter = {
		.passkeyHandler = peripheralIsMe_SendingKeys,
		.passkey = passkey,
		.keys = &keys,
		.myAddr = localBleAddress.addr,
		.otherAddr = phoneDevice.addr
	};

	// Get keys and passkey
	passkeyHandler(passkeyParameter);

	//debugPrint("Passkey: %llu", (uint64_t*) passkey);

	// Send Auth Response packet.
	Auth01_t auth01 = {};
	memcpy((uint8_t*) &auth01.keys, (uint8_t*) passkeyParameter.keys, sizeof(keys_t));
	memcpy((uint8_t*) &auth01.passkey, passkeyParameter.passkey, PASSKEY_SIZE);
	memcpy((uint8_t*) &auth01.bleMac_Phone, passkeyParameter.otherAddr, BLE_GAP_ADDR_LEN);
	memcpy((uint8_t*) &auth01.bleMac_Device, passkeyParameter.myAddr, BLE_GAP_ADDR_LEN);

	//debugPrint("Sending Peripheral Response", (uint64_t*) passkey);
	sendDataResponsePacket(Cmd_AuthPeripheralResponse, (uint8_t*)&auth01, sizeof(auth01));

	// Save last keys sent for step2
	memcpy((uint8_t*)&peripheralLastKeysSent, (uint8_t*)&keys, sizeof(keys_t));

	//debugPrint(" key1 0x%08x key2 0x%08x\r\n", keys.key1, keys.key2);
}

void peripheralAuth_step2(Auth02_t* auth02) {
	// PhonePasskey was sent, valid values. If valid, then go to next Auth step.
	debugPrint("Auth Step 2");

	// See if phone keys are different then keys this device sent in step 1.
	if (!memcmp((uint8_t*)&peripheralLastKeysSent, (uint8_t*)&auth02->keys, PASSKEY_SIZE)) {
		debugPrint("Passkey fail, same as device");
		peripheralAuth_step3(false);
		return;
	}

	// Copy keys
	keys_t keys;
	memcpy((uint8_t*)&keys, (uint8_t*)&auth02->keys, sizeof(keys_t));

	uint8_t passkey[PASSKEY_SIZE];
	PasskeyParameter_t passkeyParameter = {
		.passkeyHandler = peripheralIsMe_ReceivedKeys,
		.passkey = passkey,
		.keys = &keys,
		.myAddr = localBleAddress.addr,
		.otherAddr = phoneDevice.addr
	};

	// Get passkey for Phone keys.
	passkeyHandler(passkeyParameter);

	// Auth has completed, stop timer.
	authTimer_stop();

	// See if passkey from phone match computed.
	if (!memcmp(passkey, auth02->passkey, PASSKEY_SIZE)) {
		debugPrint("Passkey match");
		phoneDevice.hasPasskey = true;
		isAuthed = phoneDevice.hasPasskey;
		peripheralAuth_step3(true);
	} else {
		debugPrint("Passkey fail, disconnecting");
		phoneDevice.hasPasskey = false;
		isAuthed = phoneDevice.hasPasskey;
		peripheralAuth_step3(false);
		disconnectFromDevice(phoneDevice.connectionHandle);
	}
	// Note: step 3 updates generalStatusUpdate with new Auth value.
}

static void peripheralAuth_step3(bool isSuccess) {
	// Send Auth success or fail.
	debugPrint("Auth Result: %d", isSuccess);
	Auth03_t auth03 = {
		.isSuccess = isSuccess
	};

	sendDataResponsePacket(Cmd_AuthPeripheralResult, (uint8_t*)&auth03, sizeof(auth03));
}

