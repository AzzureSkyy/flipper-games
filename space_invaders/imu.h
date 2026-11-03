#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal ICM-42688-P accelerometer driver for the Flipper Video Game Module.
bool imu_init(void);
bool imu_read_accel(float out_g[3]);
void imu_deinit(void);
// Last WHO_AM_I value read by imu_init (0x47 when the sensor is present).
uint8_t imu_who_am_i(void);
