/*******************************************************************************
 * JesFs_ml.c: JesFs medium-level serial flash management

 * *
 * JesFs - Jo's Embedded Serial File System
 * (C)joembedded@gmail.com - www.joembedded.de
 *
 * Version: see jesfs.h
 *
 *******************************************************************************/

/* #include <stdio.h> */

#include <stddef.h>
#include <stdint.h>

#include "jesfs.h"
#include "jesfs_int.h"

struct sflash_info sflash_info = {
	.state_flags = STATE_DEEPSLEEP,
};

/* ------------------- Medium-level SPI start ------------------------ */
#if !defined(__ZEPHYR__)
/* Send a single-byte SPI command. More bytes may follow before deselecting. */
void sflash_bytecmd(uint8_t cmd, uint8_t more)
{
	sflash_select();
	sflash_spi_write(&cmd, 1);
	if (!more) {
		sflash_deselect();
	}
}
#endif

/* Read the 3-byte JEDEC identification. The flash must be awake first. */
#define CMD_RDID 0x9F /* Read identification: manufacturer.8 type.8 density.8. */
uint32_t sflash_quick_scan_identification(void)
{
#if !defined(__ZEPHYR__)
	uint8_t buf[3];
	uint32_t id;
	sflash_bytecmd(CMD_RDID, 1); /* More */
	sflash_spi_read(buf, 3);
	sflash_deselect();
	/* Build the 32-bit ID safely so this also works on 16-bit compilers. */
	id = buf[0];
	id <<= 8; /* Manufacturer MM */
	id |= buf[1];
	id <<= 8; /* Type TT */
	id |= buf[2]; /* Density DD */
	return id; /* 0xMMTTDD */
#else
	return zephyr_get_flash_jedec_id();

#endif
}

/* Interpret the 3-byte JEDEC ID and derive the usable flash size. */
int16_t sflash_interpret_id(uint32_t id)
{
	uint8_t h;
	sflash_info.total_flash_size = 0;

	/* Keep the flash ID for diagnostics and restart checks. */
	sflash_info.identification = id;

	if (id == 0x000000) {
		return JESFS_ERR_FLASH_ID_ZERO_SLEEP; /* Short circuit or still sleeping? */
	}
	if (id == 0xFFFFFF) {
		return JESFS_ERR_FLASH_ID_UNCONNECTED; /* Unconnected in SPI? */
	}

	switch (id >> 8) { /* Check manufacturer and type, without density. */
	default:
		return JESFS_ERR_FLASH_ID_UNKNOWN;

/* Tested/known-good flash manufacturer/type IDs. More IDs may be added later. */
	case MACRONIX_MANU_TYP_RX: /* Macronix MX25R low-power series. */
	case GIGADEV_MANU_TYP_WD: /* GigaDevice up to 8Mbit */
	case GIGADEV_MANU_TYP_WQ: /* GigaDevice >= 2Mbit */

		break; /* OK! */
	}

#ifdef DEBUG_FORCE_MINIDISK_DENSITY
	h = DEBUG_FORCE_MINIDISK_DENSITY; /* Mini disk, at least two sectors. */
#else
	h = id & 255; /* Density */
	if (h < MIN_DENSITY || h > MAX_DENSITY) {
		return JESFS_ERR_FLASH_ID_BAD_DENSITY; /* Unknown density. */
	}
#endif
	sflash_info.total_flash_size = 1 << h;
	return 0;
}

#if !defined(__ZEPHYR__)
/* Put the flash into deep power down. */
#define CMD_DEEPPOWERDOWN 0xB9
void sflash_deep_power_down(void)
{
	sflash_bytecmd(CMD_DEEPPOWERDOWN, 0); /* NoMore */
}

/*
 * Release the flash from deep power down.
 *
 * Some older devices return an ID with this command, newer ones only wake up.
 * Respect
 * the device-specific wakeup time before reading the ID again.
 */
#define CMD_RELEASEDPD 0xAB
void sflash_release_from_deep_power_down(void)
{
	sflash_bytecmd(CMD_RELEASEDPD, 0); /* NoMore */
	/* Delay is handled by the caller. */
}
#endif /* __ZEPHYR__ */

/*
 * Read len bytes from flash address sadr into sbuf.
 * For addresses >=16MB the bare-metal driver needs the 4-byte command (0x13).
 */
#define CMD_READDATA 0x03
int16_t sflash_read(uint32_t sadr, uint8_t *sbuf, uint16_t len)
{
#if !defined(__ZEPHYR__)
	uint8_t buf[4]; /* */
	buf[0] = CMD_READDATA;
	buf[1] = (uint8_t)(sadr >> 16);
	buf[2] = (uint8_t)(sadr >> 8);
	buf[3] = (uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_spi_read(sbuf, len);
	sflash_deselect();
	return 0; /* Direct SPI access reports no error here. */
#else
	return zephyr_flash_read(sadr, sbuf, len);
#endif
}

#if !defined(__ZEPHYR__)
/* Read status register. Bit 0: write in progress, bit 1: write enabled. */
#define CMD_STATUSREG 0x05
uint8_t sflash_read_status_reg(void)
{
	uint8_t buf;
	sflash_bytecmd(CMD_STATUSREG, 1); /* More */
	sflash_spi_read(&buf, 1);
	sflash_deselect();
	return buf;
}
/* Set the flash write-enable latch. */
#define CMD_WRITEENABLE 0x06
void sflash_write_enable(void)
{
	sflash_bytecmd(CMD_WRITEENABLE, 0); /* NoMore */
}
#endif /* __ZEPHYR__ */

/*
 * Program len bytes at flash address sadr from sbuf.
 *
 * For addresses >=16MB the bare-metal driver needs the 4-byte command (0x12).

 * * The write-enable latch must be set before this call and is cleared by the
 * flash after the page program operation.
 */
#define CMD_PAGEWRITE 0x02
int16_t sflash_page_write(uint32_t sadr, const uint8_t *sbuf, uint16_t len)
{
#if !defined(__ZEPHYR__)
	uint8_t buf[4]; /* */
	buf[0] = CMD_PAGEWRITE;
	buf[1] = (uint8_t)(sadr >> 16);
	buf[2] = (uint8_t)(sadr >> 8);
	buf[3] = (uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_spi_write(sbuf, len);
	sflash_deselect();
	return 0; /* Direct SPI access reports no error here. */
#else
	return zephyr_flash_write(sadr, sbuf, len);
#endif
}

#if !defined(__ZEPHYR__)
/* Start bulk erase. Requires write-enable and can take a long time. */
#define CMD_BULKERASE 0xC7
void sflash_bulk_erase(void)
{
	sflash_bytecmd(CMD_BULKERASE, 0); /* NoMore */
}
#endif

#if !defined(__ZEPHYR__)
/* Sector erase, 4k.
 * Note: 4k erase is not available on every flash, but on almost all SPI NOR
 * devices that are useful for JesFs.
 *
 * M25P40: -
 * MX25R8035:  100 typ / 300 max msec
 * MT25QL128   50 typ / 400 max msec
 */
#define CMD_SECTOR4K_ERASE 0x20
void sflash_ll_sector_erase_4k(uint32_t sadr)
{
	uint8_t buf[4]; /* */
	buf[0] = CMD_SECTOR4K_ERASE;
	buf[1] = (uint8_t)(sadr >> 16);
	buf[2] = (uint8_t)(sadr >> 8);
	buf[3] = (uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_deselect();
}
#endif

#if !defined(__ZEPHYR__)
/* Wait until the flash is no longer busy, or until the timeout expires. */
int16_t sflash_wait_busy(uint32_t msec)
{
	while (msec--) {
		sflash_wait_usec(1000);
		if (!(sflash_read_status_reg() & 1)) {
			return 0; /* OK */
		}
	}
	return JESFS_ERR_FLASH_TIMEOUT;
}
#endif

#if !defined(__ZEPHYR__)
/* Set write-enable and verify that the latch is set. */
int16_t sflash_wait_write_enabled(void)
{
	sflash_write_enable();
	if (sflash_read_status_reg() & 2) {
		return 0;
	}
	return JESFS_ERR_WRITE_ENABLE_FAILED; /* Flash locked or write-protected. */
}
#endif /* __ZEPHYR__ */

/*
 * Write up to one sector, split into page-program operations where required.
 *
 * Bare-metal flash needs explicit page-boundary
 * handling and busy polling.
 * Zephyr's flash API handles the low-level restrictions for the configured
 * device, so the Zephyr path
 * delegates the whole range.
 */
int16_t sflash_sector_write(uint32_t sflash_adr, const uint8_t *sbuf, uint32_t len)
{
	int16_t res;

	if (sflash_adr > sflash_info.total_flash_size ||
	    len > (sflash_info.total_flash_size - sflash_adr)) {
		return JESFS_ERR_FLASH_ADDR_INVALID; /* Address range exceeds the flash. */
	}

#if !defined(__ZEPHYR__)
	uint32_t maxwrite = SF_SECTOR_PH - (sflash_adr & (SF_SECTOR_PH - 1));
	if (len > maxwrite) {
		return JESFS_ERR_BLOCK_CROSSES_SECTOR;
	}

	while (len) {
		maxwrite = 256 - (uint8_t)sflash_adr;
#ifdef SF_TX_TRANSFER_LIMIT
		if (maxwrite > SF_TX_TRANSFER_LIMIT) {
			maxwrite = SF_TX_TRANSFER_LIMIT;
		}
#endif
		if (len < maxwrite) {
			maxwrite = len;
		}
		if (sflash_wait_write_enabled()) {
			return JESFS_ERR_WRITE_ENABLE_FAILED;
		}
		res = sflash_page_write(sflash_adr, sbuf, maxwrite);
		if (res) {
			return res;
		}
		if (sflash_wait_busy(100)) {
			return JESFS_ERR_FLASH_TIMEOUT; /* 100 ms until page program should be done. */
		}
		sbuf += maxwrite;
		sflash_adr += maxwrite;
		len -= maxwrite;
	}
#else
	/* Zephyr has its own boundary and alignment handling. */
	res = sflash_page_write(sflash_adr, sbuf, len);
	if (res) {
		return res;
	}
#endif
	return 0;
}

/* Erase one JesFs sector, including the required low-level checks. */
int16_t sflash_sector_erase(uint32_t sadr)
{
#if !defined(__ZEPHYR__)
	if (sflash_wait_write_enabled()) {
		return JESFS_ERR_WRITE_ENABLE_FAILED;
	}
	sflash_ll_sector_erase_4k(sadr);
	if (sflash_wait_busy(400)) {
		return JESFS_ERR_FLASH_TIMEOUT; /* 400 msec max page */
	}
	return 0;
#else
	return zephyr_flash_erase(sadr, SF_SECTOR_PH);
#endif
}
/* ------------------- Medium-level SPI OK ------------------------ */
