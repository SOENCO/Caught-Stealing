
#ifndef DW_COMMON_H
#define DW_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "app_timer.h"


#define UUS_TO_DWT_TIME 65536

#define TX_TO_RX_DELAY_UUS		60
#define FINAL_RX_TIMEOUT_UUS	3300
#define SPEED_OF_LIGHT			299702547	// Speed of light in air, meters/second

// Default antenna delay values for 64 MHz PRF.
#define TX_ANT_DLY 16480	// For use with Rev 5 design
#define RX_ANT_DLY 16480	// For use with Rev 5 design
//#define TX_ANT_DLY 16460	// For use with Rev 4 design
//#define RX_ANT_DLY 16460	// For use with Rev 4 design
//#define TX_ANT_DLY 16475	// With no TX power config (ie didn't call dwt_configuretxrf)
//#define RX_ANT_DLY 16475	// With no TX power config (ie didn't call dwt_configuretxrf)
// #define TX_ANT_DLY 16500	// With TX power config: .PGdly = 0xC2, .power = 0x67676767
// #define RX_ANT_DLY 16500	// With TX power config: .PGdly = 0xC2, .power = 0x67676767

// This is the delay from the end of the frame transmission to the enable of the receiver, as programmed for the DW1000's wait for response feature.
#define RESP_TX_TO_FINAL_RX_DLY_UUS 500

// Preamble timeout, in multiple of PAC size. See NOTE 6 below.
#define PRE_TIMEOUT 8

#define FRAME_CONTROL		0x8841
#define PAN_ID				0xDECA


typedef enum {
	MsgType_poll = 0x21,
	MsgType_reply = 0x10,
	MsgType_final = 0x23,
	MsgType_invalid = 0xFF,
} MsgType_t;

typedef enum {
	ActivityCode_ContinueRanging = 0x02,
} ActivityCode_t;

typedef PACKED_STRUCT {
	uint32_t pollTx;
    uint32_t replyRx;
    uint32_t finalTx;
} TimeStamps32_t;

typedef PACKED_STRUCT {
	uint64_t pollTx;
    uint64_t replyRx;
    uint64_t finalTx;
} TimeStamps64_t;

typedef PACKED_STRUCT {
	uint16_t frameControl;		// FRAME_CONTROL
	uint8_t sequenceNumber;		// Using this to validate the 3 packets are the same sequence.
	uint16_t panId;				// PAN_ID
	uint16_t destinationAddr;
	uint16_t sourceAddr;
	MsgType_t msgType;
} PacketHeader_t;

typedef PACKED_STRUCT {
	PacketHeader_t header;
    uint8_t data[14];
	// Note: last 2 bytes is checksum auto-set by DW1000.
} PacketGeneric_t;

typedef PACKED_STRUCT {
	PacketHeader_t header;
    uint16_t checksum;
} PacketPoll_t;

typedef PACKED_STRUCT {
	PacketHeader_t header;
	ActivityCode_t activityCode;
    uint16_t activityParameter;
    uint16_t checksum;
} PacketReply_t;

typedef PACKED_STRUCT {
	PacketHeader_t header;
	TimeStamps32_t timestamps;
    uint16_t checksum;
} PacketFinal_t;

#define PACKET_SIZE_MIN		sizeof(PacketPoll_t)

typedef PACKED_STRUCT {
	uint8_t sequenceNumber;
	uint16_t otherAddr;
	int32_t pollTx;
	uint64_t pollRx;
    uint64_t replyTx;
    int32_t replyRx;
    int32_t finalTx;
    uint64_t finalRx;
} CompleteSequence_t;

// ******** SYS_STATE ************
typedef enum {
	PmscState_Init = 0,
	PmscState_Idle = 1,
	PmscState_TxWait = 2,
	PmscState_RxWait = 3,
	PmscState_Tx = 4,
	PmscState_Rx = 5,
} PmscState_t;

typedef PACKED_STRUCT {
	uint8_t txState;
	uint8_t rxState;
	PmscState_t pmscState;
	uint8_t reserved;
} SystemState_t;

// *******************************


typedef enum {
	RxStatus_NewPacket = 0,
	RxStatus_Timeout,
	RxStatus_Error,
	RxStatus_Waiting,
} RxStatus_t;

// Public Variables
extern dwt_config_t config;
extern bool isStreamingDistance;
extern bool streamAccelerometer;


// Public Function Declarations
void dwInit(void);
void dwServiceInterrupt(void);
bool sendPollMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr);
bool sendReplyMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr, uint32_t txDelay);
void sendFinalMsg(uint8_t sequenceNumber, uint16_t destinationAddr, uint16_t sourceAddr, TimeStamps64_t timestamps);
void processPacket(const dwt_cb_data_t *cbData);
void clearErrors(void);
uint16_t getMyAddress(void);
uint16_t getOthersRxAddress(void);
bool rxEnable(int mode);
bool txStart(int mode);
void dwSetSleep(bool sleep);

#endif
