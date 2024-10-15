#ifndef _LIS2DW12_H
#define _LIS2DW12_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>

#define ADDR_OUT_T_L              0x0D
#define ADDR_OUT_T_H              0x0E
#define ADDR_WHO_AM_I             0x0F

#define ADDR_CTRL1                0x20
#define ADDR_CTRL2                0x21
#define ADDR_CTRL3                0x22
#define ADDR_CTRL4_INT1_PAD_CTRL  0x23
#define ADDR_CTRL5_INT2_PAD_CTRL  0x24
#define ADDR_CTRL6                0x25
#define ADDR_OUT_T                0x26
#define ADDR_STATUS               0x27
#define ADDR_OUT_X_L              0x28
#define ADDR_OUT_X_H              0x29
#define ADDR_OUT_Y_L              0x2A
#define ADDR_OUT_Y_H              0x2B
#define ADDR_OUT_Z_L              0x2C
#define ADDR_OUT_Z_H              0x2D
#define ADDR_FIFO_CTRL            0x2E
#define ADDR_FIFO_SAMPLES         0x2F
#define ADDR_TAP_THS_X            0x30
#define ADDR_TAP_THS_Y            0x31
#define ADDR_TAP_THS_Z            0x32
#define ADDR_INT_DUR              0x33
#define ADDR_WAKE_UP_THS          0x34
#define ADDR_WAKE_UP_DUR          0x35
#define ADDR_FREE_FALL            0x36
#define ADDR_STATUS_DUP           0x37
#define ADDR_WAKE_UP_SRC          0x38
#define ADDR_TAP_SRC              0x39
#define ADDR_SIXD_SRC             0x3A
#define ADDR_ALL_INT_SRC          0x3B
#define ADDR_X_OFS_USR            0x3C
#define ADDR_Y_OFS_USR            0x3D
#define ADDR_Z_OFS_USR            0x3E
#define ADDR_CTRL7                0x3F


// Public Variables
extern bool isShaken;

typedef enum {
	Axis_X = 0,
	Axis_Y,
	Axis_Z,
	Axis_ENUM_COUNT
} Axis_t;

typedef PACKED_STRUCT {
	int16_t x;
	int16_t y;
	int16_t z;
} XYZ_t;

// Public Function Declarations
uint8_t accelInit(void);
void accelLoop(void);
void readXYZ(void) ;
int16_t getAxis(Axis_t axis, XYZ_t xyz);
void accelSetSleep(bool sleep);
XYZ_t getXYZ(void);

#endif //_LIS2DW12_H