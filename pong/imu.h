#pragma once

#include <stdbool.h>

// Minimal ICM-42688-P accelerometer driver for the Flipper Video Game Module.
bool imu_init(void);
bool imu_read_accel(float out_g[3]);
void imu_deinit(void);
