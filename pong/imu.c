#include "imu.h"

#include <furi.h>
#include <furi_hal.h>

#define IMU_SPI (&furi_hal_spi_bus_handle_external)
#define IMU_TIMEOUT 50

#define REG_DEVICE_CONFIG 0x11
#define REG_ACCEL_DATA_X1 0x1F
#define REG_PWR_MGMT0 0x4E
#define REG_ACCEL_CONFIG0 0x50
#define REG_WHO_AM_I 0x75
#define REG_BANK_SEL 0x76
#define WHO_AM_I_VALUE 0x47

#define ACCEL_LSB_PER_G 16384.0f

static void imu_write(uint8_t reg, uint8_t val) {
	uint8_t tx[2] = {reg & 0x7F, val};
	furi_hal_spi_acquire(IMU_SPI);
	furi_hal_spi_bus_tx(IMU_SPI, tx, sizeof(tx), IMU_TIMEOUT);
	furi_hal_spi_release(IMU_SPI);
}

static bool imu_read(uint8_t reg, uint8_t* data, size_t len) {
	uint8_t tx[8] = {0};
	uint8_t rx[8] = {0};
	if(len > sizeof(tx) - 1) return false;
	tx[0] = reg | 0x80;
	furi_hal_spi_acquire(IMU_SPI);
	bool ok = furi_hal_spi_bus_trx(IMU_SPI, tx, rx, len + 1, IMU_TIMEOUT);
	furi_hal_spi_release(IMU_SPI);
	memcpy(data, rx + 1, len);
	return ok;
}

static bool imu_otg_enabled_by_us = false;
static bool imu_spi_ready = false;
static uint8_t imu_last_who = 0;

uint8_t imu_who_am_i(void) {
	return imu_last_who;
}

bool imu_init(void) {
	if(!furi_hal_power_is_otg_enabled()) {
		imu_otg_enabled_by_us = furi_hal_power_enable_otg();
		furi_delay_ms(50);
	}
	furi_hal_spi_bus_handle_init(IMU_SPI);
	imu_spi_ready = true;

	uint8_t who = 0;
	for(int attempt = 0; attempt < 5 && who != WHO_AM_I_VALUE; attempt++) {
		imu_write(REG_BANK_SEL, 0x00);
		imu_read(REG_WHO_AM_I, &who, 1);
		if(who != WHO_AM_I_VALUE) furi_delay_ms(10);
	}
	imu_last_who = who;
	if(who != WHO_AM_I_VALUE) {
		imu_deinit();
		return false;
	}

	imu_write(REG_DEVICE_CONFIG, 0x01);
	furi_delay_ms(2);
	imu_write(REG_BANK_SEL, 0x00);
	// +/-2g full scale, 1 kHz output data rate
	imu_write(REG_ACCEL_CONFIG0, 0x66);
	// Accelerometer in low-noise mode
	imu_write(REG_PWR_MGMT0, 0x03);
	furi_delay_ms(2);
	return true;
}

bool imu_read_accel(float out_g[3]) {
	uint8_t b[6];
	if(!imu_read(REG_ACCEL_DATA_X1, b, sizeof(b))) return false;
	for(int i = 0; i < 3; i++) {
		int16_t raw = (int16_t)((b[i * 2] << 8) | b[i * 2 + 1]);
		out_g[i] = raw / ACCEL_LSB_PER_G;
	}
	return true;
}

void imu_deinit(void) {
	if(imu_spi_ready) {
		imu_write(REG_PWR_MGMT0, 0x00);
		furi_hal_spi_bus_handle_deinit(IMU_SPI);
		imu_spi_ready = false;
	}
	if(imu_otg_enabled_by_us) {
		furi_hal_power_disable_otg();
		imu_otg_enabled_by_us = false;
	}
}
