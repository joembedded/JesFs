/*
 * Zephyr low-level wrapper for JesFs.
 *
 * This file is the only JesFs layer that talks directly to Zephyr's flash and
 * runtime-PM APIs. The medium and high layers keep using JesFs-style addresses
 * and error codes.
 *
 * ToKnow:
 * 'Auto-Power-Down-Dwell' time can be configured for the driver. Default:
 * CONFIG_SPI_NOR_ACTIVE_DWELL_MS=10
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/drivers/flash.h>

#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#include <jesfs.h>
#include <jesfs_int.h>

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(jedec_spi_nor), "No okay jedec,spi-nor node in devicetree");
#define SPI_FLASH_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(jedec_spi_nor)
#if defined(JESFS_EXPORT_FLASH_DEV)
const struct device *const jesfs_flash_dev = DEVICE_DT_GET(SPI_FLASH_NODE);
#else
static const struct device *const jesfs_flash_dev = DEVICE_DT_GET(SPI_FLASH_NODE);
#endif

#define SPI_FLASH_JEDEC_ID_LEN 3U

static int16_t zephyr_check_device_ready(void)
{
	if (!device_is_ready(jesfs_flash_dev)) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}
	return 0;
}

const struct device *zephyr_get_flash_device(void)
{
	if (zephyr_check_device_ready() != 0) {
		return NULL;
	}
	return jesfs_flash_dev;
}

int16_t zephyr_flash_wake(void)
{
	int rc;

	if (zephyr_check_device_ready() != 0) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	rc = pm_device_runtime_get(jesfs_flash_dev);
	return (int16_t)rc;
}

int16_t zephyr_flash_deepsleep(void)
{
	int rc;

	if (zephyr_check_device_ready() != 0) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	rc = pm_device_runtime_put(jesfs_flash_dev);
	return (int16_t)rc;
}

uint32_t zephyr_get_flash_jedec_id(void)
{
	uint8_t found_jedec_id[SPI_FLASH_JEDEC_ID_LEN];

	if (zephyr_check_device_ready() != 0) {
		return 0xFFFFFFFF;
	}

	if (flash_read_jedec_id(jesfs_flash_dev, found_jedec_id) != 0) {
		return 0xFFFFFFFF; /* Cannot read the JEDEC ID. */
	}
	uint32_t id = found_jedec_id[0] << 16 | found_jedec_id[1] << 8 | found_jedec_id[2];
	return id;
}

int16_t zephyr_flash_read(uint32_t sadr, uint8_t *sbuf, uint32_t len)
{
	int err;

	if (zephyr_check_device_ready() != 0) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	err = flash_read(jesfs_flash_dev, sadr, sbuf, len);
	return (int16_t)err;
}

int16_t zephyr_flash_write(uint32_t sadr, const uint8_t *sbuf, uint32_t len)
{
	int err;

	if (zephyr_check_device_ready() != 0) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	err = flash_write(jesfs_flash_dev, sadr, sbuf, len);
	return (int16_t)err;
}

int16_t zephyr_flash_erase(uint32_t sadr, uint32_t len)
{
	int err;

	if (zephyr_check_device_ready() != 0) {
		return JESFS_ERR_FLASH_NOT_ACCESSIBLE;
	}

	err = flash_erase(jesfs_flash_dev, sadr, len);
	return (int16_t)err;
}
