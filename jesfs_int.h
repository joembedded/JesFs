/*******************************************************************************
 * JesFs_int.h - internal JesFs interfaces
 *
 * JesFs - Jo's Embedded Serial File System
 *
 * These declarations are shared between the high, medium, and low layers.
 * Application code should include jesfs.h instead.
 * (C) joembedded@gmail.com - www.joembedded.de
 *
 * Version: see jesfs.h
 *
 *******************************************************************************/

#ifndef JESFS_INT_H
#define JESFS_INT_H

#ifdef __cplusplus
extern "C" {
#endif
/*------------------- Internal JesFs defines ------------------------*/

/*
 * Debug/stress option: treat the SPI flash as a very small disk. Size must be
 * sector_size^x and at least two sectors (=8kB). Maximum is 2GB; with 3-byte
 * SPI flash addressing the practical maximum is 16MB.
 */
/* #define DEBUG_FORCE_MINIDISK_DENSITY 0x0F */

#define MIN_DENSITY 0x0D
#define MAX_DENSITY 0x18

/* Header at the beginning of every sector. */
#define HEADER_SIZE_L 3
#define HEADER_SIZE_B (HEADER_SIZE_L * 4)
/* Additional file metadata after the HEAD sector header. */
#define FINFO_SIZE_B 36

/*------------------- Internal JesFs constants and functions ------------------------*/

#if !defined(__ZEPHYR__)
/* Historic JesFs sector magic values, optimized for programming bits from 1 to 0. */
#define HEADER_MAGIC 0x4673654A
#define SECTOR_MAGIC_HEAD_ACTIVE 0xFFFF293A
#define SECTOR_MAGIC_HEAD_DELETED 0xFFFF2130
#define SECTOR_MAGIC_DATA 0xFFFF5D5B
#define SECTOR_MAGIC_TODELETE 0xFFFF4040
#else
/* Zephyr port: byte-oriented little-endian magic values for readable hex dumps. */
#define HEADER_MAGIC 0x4673654A
#define SECTOR_MAGIC_HEAD_ACTIVE 0xFF416548
#define SECTOR_MAGIC_HEAD_DELETED 0x78416548
#define SECTOR_MAGIC_DATA 0xFF446154
#define SECTOR_MAGIC_TODELETE 0x78446154

#endif

/* Convenient byte/word/long access helper. */
union sflash_long_word_byte {
	uint8_t u8[4];
	uint16_t u16[2];
	uint32_t u32;
};

/*------------------- Hardware-dependent low-level flash functions -------------------*/
#if !defined(__ZEPHYR__)
/* Other systems: direct byte-level SPI access. */
void sflash_wait_usec(uint32_t usec);

int16_t sflash_spi_init(void);
void sflash_spi_close(void);

void sflash_select(void);
void sflash_deselect(void);
void sflash_spi_read(uint8_t *buf, uint16_t len);
void sflash_spi_write(const uint8_t *buf, uint16_t len);
#else
#if defined(JESFS_EXPORT_FLASH_DEV)
extern const struct device *const jesfs_flash_dev;
#endif
/* Direct low-level access through Zephyr's flash API. */
const struct device *zephyr_get_flash_device(void);
int16_t zephyr_flash_wake(void);
int16_t zephyr_flash_deepsleep(void);

uint32_t zephyr_get_flash_jedec_id(void);

int16_t zephyr_flash_read(uint32_t sadr, uint8_t *sbuf, uint32_t len);
int16_t zephyr_flash_write(uint32_t sadr, const uint8_t *sbuf, uint32_t len);
int16_t zephyr_flash_erase(uint32_t sadr, uint32_t len);
#endif

/*---------------- Small helpers for the medium layer ----------------*/
uint32_t jesfs_strlen(const char *s);
void jesfs_strncpy(char *d, const char *s, uint8_t maxchar);
void jesfs_memset(uint8_t *p, uint8_t v, uint32_t n);
int16_t jesfs_strcmp(const char *s1, const char *s2);
uint32_t jesfs_track_crc32(const uint8_t *pdata, uint32_t wlen, uint32_t crc_run);
uint32_t jesfs_get_secs(void);

/*------------------- Medium-level serial flash functions ------------------------*/
#if !defined(__ZEPHYR__)
/* SPI byte access. */
void sflash_bytecmd(uint8_t cmd, uint8_t more);
void sflash_deep_power_down(void);
void sflash_release_from_deep_power_down(void);
uint8_t sflash_read_status_reg(void);
void sflash_write_enable(void);
void sflash_bulk_erase(void);

#else
void sflash_ll_sector_erase_4k(uint32_t sadr);
#endif

uint32_t sflash_quick_scan_identification(void);
int16_t sflash_interpret_id(uint32_t id);

int16_t sflash_read(uint32_t sadr, uint8_t *sbuf, uint16_t len);
int16_t sflash_page_write(uint32_t sadr, const uint8_t *sbuf, uint16_t len);

int16_t sflash_wait_busy(uint32_t msec);
int16_t sflash_wait_write_enabled(void);
int16_t sflash_sector_write(uint32_t sflash_adr, const uint8_t *sbuf, uint32_t len);
int16_t sflash_sector_erase(uint32_t sadr);
#ifdef __cplusplus
}
#endif
#endif /* JESFS_INT_H */
/*-------------- End of internal JesFs functions ------------------------*/
