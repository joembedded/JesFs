/*******************************************************************************
 * JesFs.h: Header Files for JesFs
 *
 * JesFs - Jo's Embedded Serial File System
 *
 * (C) joembedded@gmail.com - www.joembedded.de
 *
 * Versions:
 * 0.0  / ca. 1994 - Preliminary versions
 * 1.0  / 16.09.2016 Initial Version on TI's CC1310
 * 1.5  / 25.11.2019 Open Sourced on GitHub
 * 1.6  / 22.12.2019 added jesfs_check_disk()
 * 1.7  / 12.03.2020 added jesfs_date_to_sec1970()
 * 1.8  / 25.09.2020 added jesfs_set_static_secs() to set a static time for JesFs
 * 1.81 / 19.12.2020 redundant code removed in jesfs_date_to_sec1970()
 * 1.82 / 21.03.2021 added comment jesfs_format() Timeouts
 * 1.83 / 11.07.2021 added Pin Definitions for NRF52
 * 1.84 / 15.08.2021 check in jesfs_date_to_sec1970()
 * 1.85 / 17.03.2022 added check (Warren)
 * 1.86 / 18.03.2022 corrected bug in jesfs_date_to_sec1970()
 * 1.87 / 02.04.2022 jesfs_strcpy()->jesfs_strncpy() and some minor opts.
 * 1.88 / 17.03.2023 added feature jesfs_supply_voltage_check()
 * 1.89 / 14.09.2023 all global jesfs_-functions check jesfs_supply_voltage_check() on entry
 * 1.90 / 29.09.2024 cosmetics
 * 1.91 / 14.10.2024 fixed bug from 1.90
 * 1.92 / 20.02.2025 added jesfs_notexists()
 * 1.93 / 21.06.2026 reformatted errors for better reading and configurable JESFS_ERR_BASE
 * 1.94 / 21.06.2026 hardened jesfs_read(), jesfs_check_disk(), and jesfs_open()
 * ----
 * 2.00 / 21.06.2026 Zephyr-OS port, added jesfs_is_awake()
 *
 *******************************************************************************/

#ifndef JESFS_H
#define JESFS_H

/* Error overview
 *
 * SPI / Flash HW access
 * - JESFS_ERR_SPI_INIT                  : SPI init failed (hardware)
 * - JESFS_ERR_FLASH_TIMEOUT             : Flash timeout while waiting for busy=0
 * - JESFS_ERR_WRITE_ENABLE_FAILED       : Could not set flash WriteEnable bit (flash locked?)
 * - JESFS_ERR_FLASH_ADDR_INVALID        : Illegal flash address
 * - JESFS_ERR_BLOCK_CROSSES_SECTOR      : Block crosses sector border
 * - JESFS_ERR_SECTOR_BORDER_VIOLATED    : Sector border violated (before write)
 * - JESFS_ERR_ERASE_FAILED              : Sector erase failed
 * - JESFS_ERR_WRITE_FAILED              : Flash write failed
 * - JESFS_ERR_VERIFY_FAILED             : Verify failed
 *
 * Flash ID / connectivity
 * - JESFS_ERR_FLASH_ID_BAD_DENSITY      : Flash ID readable, but density unknown/illegal
 * - JESFS_ERR_FLASH_ID_UNKNOWN          : Flash ID readable, but manufacturer/type unknown
 * - JESFS_ERR_FLASH_ID_MISMATCH         : Flash ID in index differs from hardware ID
 * - JESFS_ERR_FLASH_ID_ZERO_SLEEP       : Flash ID read as 0x000000
 * - JESFS_ERR_FLASH_ID_UNCONNECTED      : Flash ID read as 0xFFFFFF
 *
 * Filesystem layout / integrity
 * - JESFS_ERR_FS_STRUCTURE_PROBLEM      : jesfs_start found structural filesystem problems
 * - JESFS_ERR_BAD_MAGIC                 : Unknown magic (unformatted flash or different data)
 * - JESFS_ERR_INDEX_CORRUPTED           : Index corrupted
 * - JESFS_ERR_BAD_FS_STRUCTURE          : Illegal filesystem structure
 * - JESFS_ERR_BAD_INDEX_HEAD            : Index points to illegal HEAD
 * - JESFS_ERR_BAD_INDEX_ENTRY           : Illegal filesystem structure, bad index entry
 * - JESFS_ERR_BAD_MAGIC_HEADER          : Illegal magic header value (and not 0xFFFFFFFF)
 * - JESFS_ERR_SECTOR_LIST_CYCLE         : Short cycle in sector list (run recover)
 * - JESFS_ERR_BAD_SECTOR_OWNER          : Sector list contains illegal file owner (run recover)
 * - JESFS_ERR_BAD_SECTOR_TYPE           : Illegal sector type (run recover)
 * - JESFS_ERR_SECTOR_HEADER_OWNER       : Sector defect ('header with owner') (run recover)
 * - JESFS_ERR_EMPTY_SECTOR_NOT_EMPTY    : Corrupted sector: empty-marked sector is not empty
 *
 * Capacity / indexing / metadata
 * - JESFS_ERR_BAD_FILENAME              : Filename too long/short
 * - JESFS_ERR_INDEX_FULL                : Too many files, index full (about 1000 with 4k sectors)
 * - JESFS_ERR_NO_FREE_SECTOR            : Flash full (no free sectors) or flash not formatted
 * - JESFS_ERR_STAT_INDEX_RANGE          : struct jesfs_stat index out of range
 * - JESFS_ERR_STAT_NO_ACTIVE_FILE       : struct jesfs_stat entry has no active file
 * - JESFS_ERR_INDEX_OUT_OF_RANGE        : Index out of range
 * - JESFS_ERR_BAD_SECTOR_ADDR           : Illegal sector address
 * - JESFS_ERR_FILE_EMPTY                : File is empty
 *
 * File descriptor / operation usage
 * - JESFS_ERR_BAD_DESCRIPTOR            : Illegal descriptor or file not open
 * - JESFS_ERR_NOT_OPEN_FOR_WRITE        : File not open for writing
 * - JESFS_ERR_FILE_NOT_FOUND            : File not found
 * - JESFS_ERR_BAD_FILE_FLAGS            : Illegal file flags for requested operation
 * - JESFS_ERR_CLOSED_FILE_CONTINUE      : Closed file cannot be continued for writing
 * - JESFS_ERR_FILE_DESC_CORRUPTED       : File descriptor corrupted
 * - JESFS_ERR_RAW_WRITE_UNKNOWN_END     : RAW write on unclosed file with unknown end position
 * - JESFS_ERR_RENAME_OPEN_FOR_READ_OR_RAW : Rename not possible when files are open as READ or RAW
 * - JESFS_ERR_RENAME_TARGET_NOT_EMPTY   : Rename requires an empty target file
 * - JESFS_ERR_RENAME_FILES_NOT_OPEN     : Both files must be open for rename
 *
 * State / power / command constraints
 * - JESFS_ERR_BAD_FORMAT_PARAM          : Illegal format parameter
 * - JESFS_ERR_DEEPSLEEP_ALREADY         : Deep sleep command while already sleeping (informative)
 * - JESFS_ERR_FS_SLEEPING               : Command rejected because filesystem is sleeping
 * - JESFS_ERR_VOLTAGE_TOO_LOW           : Device voltage too low
 * - JESFS_ERR_FLASH_NOT_ACCESSIBLE      : Flash not accessible (deep sleep or power fail)
 */

#include <stdint.h>

typedef int16_t jesfs_err_t;

#define JESFS_OK 0
/* Keep errors negative because positive jesfs_read() values are byte counts. */
#ifndef JESFS_ERR_BASE
#if !defined(__ZEPHYR__)
#define JESFS_ERR_BASE 100
#else
#define JESFS_ERR_BASE 1000 /* Keep clear of common POSIX errno values. */
#endif
#endif
#define JESFS_ERR(n) (-(JESFS_ERR_BASE + (n)))

#define JESFS_ERR_SPI_INIT JESFS_ERR(0)
#define JESFS_ERR_FLASH_TIMEOUT JESFS_ERR(1)
#define JESFS_ERR_WRITE_ENABLE_FAILED JESFS_ERR(2)
#define JESFS_ERR_FLASH_ID_BAD_DENSITY JESFS_ERR(3)
#define JESFS_ERR_FLASH_ID_UNKNOWN JESFS_ERR(4)
#define JESFS_ERR_FLASH_ADDR_INVALID JESFS_ERR(5)
#define JESFS_ERR_BLOCK_CROSSES_SECTOR JESFS_ERR(6)
#define JESFS_ERR_FS_STRUCTURE_PROBLEM JESFS_ERR(7)
#define JESFS_ERR_BAD_MAGIC JESFS_ERR(8)
#define JESFS_ERR_FLASH_ID_MISMATCH JESFS_ERR(9)
#define JESFS_ERR_BAD_FILENAME JESFS_ERR(10)
#define JESFS_ERR_INDEX_FULL JESFS_ERR(11)
#define JESFS_ERR_SECTOR_BORDER_VIOLATED JESFS_ERR(12)
#define JESFS_ERR_NO_FREE_SECTOR JESFS_ERR(13)
#define JESFS_ERR_INDEX_CORRUPTED JESFS_ERR(14)
#define JESFS_ERR_STAT_INDEX_RANGE JESFS_ERR(15)
#define JESFS_ERR_STAT_NO_ACTIVE_FILE JESFS_ERR(16)
#define JESFS_ERR_BAD_DESCRIPTOR JESFS_ERR(17)
#define JESFS_ERR_NOT_OPEN_FOR_WRITE JESFS_ERR(18)
#define JESFS_ERR_INDEX_OUT_OF_RANGE JESFS_ERR(19)
#define JESFS_ERR_BAD_SECTOR_ADDR JESFS_ERR(20)
#define JESFS_ERR_SECTOR_LIST_CYCLE JESFS_ERR(21)
#define JESFS_ERR_BAD_SECTOR_OWNER JESFS_ERR(22)
#define JESFS_ERR_BAD_SECTOR_TYPE JESFS_ERR(23)
#define JESFS_ERR_FILE_NOT_FOUND JESFS_ERR(24)
#define JESFS_ERR_BAD_FILE_FLAGS JESFS_ERR(25)
#define JESFS_ERR_BAD_FS_STRUCTURE JESFS_ERR(26)
#define JESFS_ERR_CLOSED_FILE_CONTINUE JESFS_ERR(27)
#define JESFS_ERR_SECTOR_HEADER_OWNER JESFS_ERR(28)
#define JESFS_ERR_FILE_DESC_CORRUPTED JESFS_ERR(29)
#define JESFS_ERR_RAW_WRITE_UNKNOWN_END JESFS_ERR(30)
#define JESFS_ERR_EMPTY_SECTOR_NOT_EMPTY JESFS_ERR(31)
#define JESFS_ERR_FILE_EMPTY JESFS_ERR(32)
#define JESFS_ERR_RENAME_OPEN_FOR_READ_OR_RAW JESFS_ERR(33)
#define JESFS_ERR_RENAME_TARGET_NOT_EMPTY JESFS_ERR(34)
#define JESFS_ERR_RENAME_FILES_NOT_OPEN JESFS_ERR(35)
#define JESFS_ERR_ERASE_FAILED JESFS_ERR(36)
#define JESFS_ERR_WRITE_FAILED JESFS_ERR(37)
#define JESFS_ERR_VERIFY_FAILED JESFS_ERR(38)
#define JESFS_ERR_BAD_FORMAT_PARAM JESFS_ERR(39)
#define JESFS_ERR_DEEPSLEEP_ALREADY JESFS_ERR(40)
#define JESFS_ERR_FS_SLEEPING JESFS_ERR(41)
#define JESFS_ERR_BAD_INDEX_HEAD JESFS_ERR(42)
#define JESFS_ERR_BAD_INDEX_ENTRY JESFS_ERR(43)
#define JESFS_ERR_FLASH_ID_ZERO_SLEEP JESFS_ERR(44)
#define JESFS_ERR_FLASH_ID_UNCONNECTED JESFS_ERR(45)
#define JESFS_ERR_BAD_MAGIC_HEADER JESFS_ERR(46)
#define JESFS_ERR_VOLTAGE_TOO_LOW JESFS_ERR(47)
#define JESFS_ERR_FLASH_NOT_ACCESSIBLE JESFS_ERR(48)

#ifdef __cplusplus
extern "C" {
#endif

/*------------------- Area for User Settings START -----------------------------*/
/* SF_xx_TRANSFER_LIMIT:
 * If defined, SPI Read- and Write-Transfers are chunked to this maximum Limit
 * for normal operation set to maximum or undefine ;-)
 * Recommended for Read to CPU: use >=64, or better: undefine SF_RD_TRANSFER_LIMIT
 * for Write to SPI: Because standard SPI has 256-Byte pages, chunks are
 * already small. But feel free to set SF_TX_TRANSFER_LIMIT to somethig smaller
 *
 * On TI-RTOS and the X110-Emulator on a CC1310 the heap does not like
 * too big chunks. Smaller chunks make transfers slower.
 */
/* #define SF_RD_TRANSFER_LIMIT 64 */
/* #define SF_TX_TRANSFER_LIMIT 64 */

/* Define this macro for additional statistics. */
#define JSTAT

/* Supported flash JEDEC IDs (format 0xMMTTDD). */

#define MACRONIX_MANU_TYP_RX 0xC228
/* #define GIGADEV_MANU_TYP_RC 0xC840 */
#define GIGADEV_MANU_TYP_WD 0xC864
#define GIGADEV_MANU_TYP_WQ 0xC865

/*------------------- Area for User Settings END -------------------------------*/

#define FNAMELEN 21

/* Start flags for jesfs_start(). */
#define FS_START_NORMAL 0
#define FS_START_FAST 1
/* #define FS_START_PEDANTIC 2 */
#define FS_START_RESTART 128

#if !defined(__ZEPHYR__)
#define FS_FORMAT_FULL 1 /* Full format (= Bulk Erase) is slow ans old-style, but easy to use */
#endif
#define FS_FORMAT_SOFT 2 /* Soft format (= per Sector) is more transparent and more modern */

/* Flags for jesfs_open(). */
#define SF_OPEN_READ 1
#define SF_OPEN_CREATE 2
#define SF_OPEN_WRITE 4
#define SF_OPEN_RAW 8
#define SF_OPEN_CRC 16

#define SF_XOPEN_UNCLOSED 32

#define SF_OPEN_EXT_SYNC 64
#define _SF_OPEN_RES 128

/* Flags returned by jesfs_info(). */
#define FS_STAT_ACTIVE 1
#define FS_STAT_INACTIVE 2
#define FS_STAT_UNCLOSED 4
#define FS_STAT_INDEX 128

/* Flags in struct sflash_info.state_flags. */
#define STATE_DEEPSLEEP 1
#define STATE_POWERFAIL 2
#define STATE_DEEPSLEEP_OR_POWERFAIL (STATE_DEEPSLEEP | STATE_POWERFAIL)

/**
 * @brief Open file descriptor.
 *
 * Members prefixed with an underscore are owned by JesFs and must not be
 * modified by callers.
 */
struct jesfs_desc {
	uint32_t _head_sadr;
	uint32_t _wrk_sadr;
	uint32_t file_pos;
	uint32_t file_len;
	uint32_t file_crc32;
	uint32_t file_ctime;
	uint16_t _sadr_rel;
	uint8_t open_flags;
};

/** File statistic descriptor returned by jesfs_info(). */
struct jesfs_stat {
	char fname[FNAMELEN + 1];
	uint32_t file_ctime;
	uint32_t file_len;
	uint32_t file_crc32;
	uint32_t _head_sadr;
	uint8_t disk_flags;
};

/* Standard sizes for this implementation, see documentation. */
#define SF_SECTOR_PH 4096
/* SF_BUFFER_SIZE_B 128 = 32 uint32_t values. Recommended minimum: 64 bytes. */
#if (!defined(__ZEPHYR__))
#define SF_BUFFER_SIZE_B 128
#else
#define SF_BUFFER_SIZE_B 256
#endif

/* Working flash buffer, used for finding EOF and for intra-sector copies. */
union sflash_buffer {
	uint8_t u8[SF_BUFFER_SIZE_B];
	uint16_t u16[SF_BUFFER_SIZE_B / 2];
	uint32_t u32[SF_BUFFER_SIZE_B / 4];
};

/** Global flash/filesystem state. */
struct sflash_info {
	uint32_t identification;
	uint32_t total_flash_size;
	uint32_t creation_date;
	uint32_t lusect_adr;
	uint32_t available_disk_size;
	uint16_t files_used;
	uint16_t files_active;

#ifdef JSTAT
	uint16_t sectors_todelete;
	uint16_t sectors_clear;
	uint16_t sectors_unknown;
#endif

	union sflash_buffer databuf;
	uint8_t state_flags;
};

extern struct sflash_info sflash_info;

/** Readable date representation used by jesfs_sec1970_to_date(). */
struct jesfs_date {
	uint8_t sec;
	uint8_t min;
	uint8_t h;
	uint8_t d;
	uint8_t m;
	uint16_t a;
};

/** @defgroup jesfs JesFs filesystem API
 *  @{
 */

/** Start or wake the filesystem and scan its on-flash metadata. */
int16_t jesfs_start(uint8_t mode);

/** Put the flash/filesystem into low-power mode. */
int16_t jesfs_deepsleep(void);

/** Check if the filesystem is already awake. */
int16_t jesfs_is_awake(void);

#if !defined(__ZEPHYR__)
/** Format the filesystem. */
int16_t jesfs_format(uint8_t fmode);
#else
/** Format the filesystem and optionally report sector progress. */
int16_t jesfs_format(uint8_t fmode, void cb_prog(uint32_t cur_sect, uint32_t total_sect));
#endif

/** Read data or advance the descriptor when pdest is NULL. */
int32_t jesfs_read(struct jesfs_desc *pdesc, uint8_t *pdest, uint32_t len);

/** Rewind an open read descriptor. */
int16_t jesfs_rewind(struct jesfs_desc *pdesc);

/** Open or create a file. */
int16_t jesfs_open(struct jesfs_desc *pdesc, const char *pname, uint8_t flags);

/** Return 0 when a file exists, otherwise a negative JesFs error. */
int16_t jesfs_notexists(const char *pname);

/** Write data to an open descriptor. */
int16_t jesfs_write(struct jesfs_desc *pdesc, const uint8_t *pdata, uint32_t len);

/** Close and finalize an open descriptor. */
int16_t jesfs_close(struct jesfs_desc *pdesc);

/** Mark an opened file as deleted. */
int16_t jesfs_delete(struct jesfs_desc *pdesc);

/** Rename a file using an opened source descriptor and staging target descriptor. */
int16_t jesfs_rename(struct jesfs_desc *pd_odesc, struct jesfs_desc *pd_ndesc);

/** Read the stored CRC32 from an opened descriptor. */
uint32_t jesfs_get_crc32(struct jesfs_desc *pdesc);

/** Enumerate file metadata by index. */
int16_t jesfs_info(struct jesfs_stat *pstat, uint16_t fno);

/** Convert Unix seconds to a readable JesFs date. */
void jesfs_sec1970_to_date(uint32_t asecs, struct jesfs_date *pd);

/** Convert a readable JesFs date to Unix seconds. */
uint32_t jesfs_date_to_sec1970(const struct jesfs_date *pd);

/** Override creation timestamps for deterministic tests; pass 0 to disable. */
void jesfs_set_static_secs(uint32_t newsecs);

/** Run a structural and CRC diagnostic scan. */
int16_t jesfs_check_disk(void cb_printf(const char *fmt, ...));

#if !defined(__ZEPHYR__)
/*
 * Legacy bare-metal API names.
 *
 * Keep the historic fs_* names available outside Zephyr. Zephyr builds must
 * not define these aliases because Zephyr owns the fs_* filesystem namespace.
 */
typedef struct jesfs_desc FS_DESC;
typedef struct jesfs_stat FS_STAT;
typedef struct jesfs_date FS_DATE;

#define _time_get jesfs_time_get
#define _supply_voltage_check jesfs_supply_voltage_check

#define fs_strlen jesfs_strlen
#define fs_track_crc32 jesfs_track_crc32

#define fs_start jesfs_start
#define fs_deepsleep jesfs_deepsleep
#define fs_is_awake jesfs_is_awake
#define fs_format jesfs_format
#define fs_read jesfs_read
#define fs_rewind jesfs_rewind
#define fs_open jesfs_open
#define fs_notexists jesfs_notexists
#define fs_write jesfs_write
#define fs_close jesfs_close
#define fs_delete jesfs_delete
#define fs_rename jesfs_rename
#define fs_get_crc32 jesfs_get_crc32
#define fs_info jesfs_info
#define fs_sec1970_to_date jesfs_sec1970_to_date
#define fs_date2sec1970 jesfs_date_to_sec1970
#define fs_set_static_secs jesfs_set_static_secs
#define fs_check_disk(cb_printf, pline, line_size) jesfs_check_disk(cb_printf)
#endif

/** @} */

#ifdef __cplusplus
}
#endif
#endif /* JESFS_H */
/* End */
