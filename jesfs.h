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
* 1.6  / 22.12.2019 added fs_check_disk()
* 1.7  / 12.03.2020 added fs_date2sec1970()
* 1.8  / 25.09.2020 added fs_set_static_secs() to set a static time for JesFs
* 1.81 / 19.12.2020 redundant code removed in fs_date2sec1970()
* 1.82 / 21.03.2021 added comment fs_format() Timeouts
* 1.83 / 11.07.2021 added Pin Definitions for NRF52
* 1.84 / 15.08.2021 check in fs_date2sec1970()
* 1.85 / 17.03.2022 added check (Warren)
* 1.86 / 18.03.2022 corrected bug in fs_date2sec1970()
* 1.87 / 02.04.2022 fs_strcpy()->fs_strncpy() and some minor opts.
* 1.88 / 17.03.2023 added feature _supply_voltage_check()
* 1.89 / 14.09.2023 all global fs_-functions check _supply_voltage_check() on entry
* 1.90 / 29.09.2024 cosmetics
* 1.91 / 14.10.2024 fixed bug from 1.90
* 1.92 / 20.02.2025 added fs_notexists()
* 1.93 / 21.06.2026 reformatted errors for better reading and configurable JESFS_ERR_BASE
* 1.94 / 21.06.2026 hardened fs_read(), fs_check_disk(), and fs_open() 
*
*******************************************************************************/

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
 * - JESFS_ERR_FLASH_ID_MISMATCH         : Flash ID in index does not match hardware ID (run recover)
 * - JESFS_ERR_FLASH_ID_ZERO_SLEEP       : Flash ID read as 0x000000 (short on SPI or still in sleep)
 * - JESFS_ERR_FLASH_ID_UNCONNECTED      : Flash ID read as 0xFFFFFF (SPI unconnected or flash corrupt)
 *
 * Filesystem layout / integrity
 * - JESFS_ERR_FS_STRUCTURE_PROBLEM      : fs_start found structural filesystem problems (run recover)
 * - JESFS_ERR_BAD_MAGIC                 : Unknown magic (unformatted flash or different data)
 * - JESFS_ERR_INDEX_CORRUPTED           : Index corrupted (or FS in deep sleep, see STATE_DEEPSLEEP)
 * - JESFS_ERR_BAD_FS_STRUCTURE          : Illegal filesystem structure (possible power-loss side effects)
 * - JESFS_ERR_BAD_INDEX_HEAD            : Illegal filesystem structure, index points to illegal HEAD
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
 * - JESFS_ERR_STAT_INDEX_RANGE          : fs_stat index out of range
 * - JESFS_ERR_STAT_NO_ACTIVE_FILE       : fs_stat entry has no active file
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
/* Keep errors negative because positive fs_read() values are byte counts. */
#ifndef JESFS_ERR_BASE
#define JESFS_ERR_BASE 100
#endif
#define JESFS_ERR(n) (-(JESFS_ERR_BASE + (n)))

#define JESFS_ERR_SPI_INIT                    JESFS_ERR(0)
#define JESFS_ERR_FLASH_TIMEOUT              JESFS_ERR(1)
#define JESFS_ERR_WRITE_ENABLE_FAILED        JESFS_ERR(2)
#define JESFS_ERR_FLASH_ID_BAD_DENSITY       JESFS_ERR(3)
#define JESFS_ERR_FLASH_ID_UNKNOWN           JESFS_ERR(4)
#define JESFS_ERR_FLASH_ADDR_INVALID         JESFS_ERR(5)
#define JESFS_ERR_BLOCK_CROSSES_SECTOR       JESFS_ERR(6)
#define JESFS_ERR_FS_STRUCTURE_PROBLEM       JESFS_ERR(7)
#define JESFS_ERR_BAD_MAGIC                  JESFS_ERR(8)
#define JESFS_ERR_FLASH_ID_MISMATCH          JESFS_ERR(9)
#define JESFS_ERR_BAD_FILENAME               JESFS_ERR(10)
#define JESFS_ERR_INDEX_FULL                 JESFS_ERR(11)
#define JESFS_ERR_SECTOR_BORDER_VIOLATED     JESFS_ERR(12)
#define JESFS_ERR_NO_FREE_SECTOR             JESFS_ERR(13)
#define JESFS_ERR_INDEX_CORRUPTED            JESFS_ERR(14)
#define JESFS_ERR_STAT_INDEX_RANGE           JESFS_ERR(15)
#define JESFS_ERR_STAT_NO_ACTIVE_FILE        JESFS_ERR(16)
#define JESFS_ERR_BAD_DESCRIPTOR             JESFS_ERR(17)
#define JESFS_ERR_NOT_OPEN_FOR_WRITE         JESFS_ERR(18)
#define JESFS_ERR_INDEX_OUT_OF_RANGE         JESFS_ERR(19)
#define JESFS_ERR_BAD_SECTOR_ADDR            JESFS_ERR(20)
#define JESFS_ERR_SECTOR_LIST_CYCLE          JESFS_ERR(21)
#define JESFS_ERR_BAD_SECTOR_OWNER           JESFS_ERR(22)
#define JESFS_ERR_BAD_SECTOR_TYPE            JESFS_ERR(23)
#define JESFS_ERR_FILE_NOT_FOUND             JESFS_ERR(24)
#define JESFS_ERR_BAD_FILE_FLAGS             JESFS_ERR(25)
#define JESFS_ERR_BAD_FS_STRUCTURE           JESFS_ERR(26)
#define JESFS_ERR_CLOSED_FILE_CONTINUE       JESFS_ERR(27)
#define JESFS_ERR_SECTOR_HEADER_OWNER        JESFS_ERR(28)
#define JESFS_ERR_FILE_DESC_CORRUPTED        JESFS_ERR(29)
#define JESFS_ERR_RAW_WRITE_UNKNOWN_END      JESFS_ERR(30)
#define JESFS_ERR_EMPTY_SECTOR_NOT_EMPTY     JESFS_ERR(31)
#define JESFS_ERR_FILE_EMPTY                 JESFS_ERR(32)
#define JESFS_ERR_RENAME_OPEN_FOR_READ_OR_RAW JESFS_ERR(33)
#define JESFS_ERR_RENAME_TARGET_NOT_EMPTY    JESFS_ERR(34)
#define JESFS_ERR_RENAME_FILES_NOT_OPEN      JESFS_ERR(35)
#define JESFS_ERR_ERASE_FAILED               JESFS_ERR(36)
#define JESFS_ERR_WRITE_FAILED               JESFS_ERR(37)
#define JESFS_ERR_VERIFY_FAILED              JESFS_ERR(38)
#define JESFS_ERR_BAD_FORMAT_PARAM           JESFS_ERR(39)
#define JESFS_ERR_DEEPSLEEP_ALREADY          JESFS_ERR(40)
#define JESFS_ERR_FS_SLEEPING                JESFS_ERR(41)
#define JESFS_ERR_BAD_INDEX_HEAD             JESFS_ERR(42)
#define JESFS_ERR_BAD_INDEX_ENTRY            JESFS_ERR(43)
#define JESFS_ERR_FLASH_ID_ZERO_SLEEP        JESFS_ERR(44)
#define JESFS_ERR_FLASH_ID_UNCONNECTED       JESFS_ERR(45)
#define JESFS_ERR_BAD_MAGIC_HEADER           JESFS_ERR(46)
#define JESFS_ERR_VOLTAGE_TOO_LOW            JESFS_ERR(47)
#define JESFS_ERR_FLASH_NOT_ACCESSIBLE       JESFS_ERR(48)

#ifdef __cplusplus
extern "C"{
#endif

//------------------- Area for User Settings START -----------------------------
/* SF_xx_TRANSFER_LIMIT:
* If defined, SPI Read- and Write-Transfers are chunked to this maximum Limit
* for normal operation set to maximum or undefine ;-)
* Recommended for Read to CPU: use >=64, or better: undefine SF_RD_TRANSFER_LIMIT
* for Write to SPI: Because standard SPI has 256-Byte pages, chunks are
* already small. But feel free to set SF_TX_TRANSFER_LIMIT to somethig smaller
*
* On TI-RTOS and the X110-Emulator on a CC1310 the heap does not like
* too big chunks. However, it makes transfers slower.
*/
//#define SF_RD_TRANSFER_LIMIT 64
//#define SF_TX_TRANSFER_LIMIT 64

// Define this macro for additional statistics
#define JSTAT

// Sample-Flash JEDEC ID (Format 0xMMTTDD)

#define MACRONIX_MANU_TYP_RX    0xC228  // Macronix MX25R-Low-Power-Series first 2 ID-Bytes (without Density)
//#define GIGADEV_MANU_TYP_RC     0xC840  // GigaDevices (used in RC1310F) first 2 ID-Bytes (without Density), WARNING: OK, but will not work for < 3V!!!
#define GIGADEV_MANU_TYP_WD     0xC864  // GigaDevices (same as MX25R, e.g. GD25WD80C)
#define GIGADEV_MANU_TYP_WQ     0xC865  // GigaDevices (same as MX25R,e.g. GD25WQ64E)

//------------------- Area for User Settings END -------------------------------

#define FNAMELEN 21  // maximum filename len (Byte 22 must be 0, as in regular strings)...

// Startflags (fs_start())
#define FS_START_NORMAL   0 // ca. 20 msec per MB on an empty Flash
#define FS_START_FAST     1  // ca. 10 msec per MB on an empty Flash, but less checks
//#define FS_START_PEDANTIC   2 // Reserved for Version 2
#define FS_START_RESTART  128 // ca. 50 usec if Flash data is already known. Else FS_START_NORMAL

#define FS_FORMAT_FULL  1  // Erases ALL (Bulk Erase) (slow)
#define FS_FORMAT_SOFT  2  // Erase only non-empty sectors (sometimes faster)

// Flags for (fs_open) files
#define SF_OPEN_READ      1 // open for read only
#define SF_OPEN_CREATE    2 // create file in any case
#define SF_OPEN_WRITE     4 // open for writing
#define SF_OPEN_RAW       8 // just open
#define SF_OPEN_CRC    	  16 // if set: calculate CRC32 for file while reading/writing
#define SF_XOPEN_UNCLOSED  32 // File is/was not closed. Set (informative) by fs_stat() (disc_flags) or fs_open (open_flags)
#define SF_OPEN_EXT_SYNC   64 // File should be synced to external filesystem, this Flag is not relevant for the Filesystem, but for external access
#define _SF_OPEN_RES 128    // Reserved for nonvolatile files (for JesFs V2.x)

// Flags for File-List
#define FS_STAT_ACTIVE 1
#define FS_STAT_INACTIVE 2
#define FS_STAT_UNCLOSED 4
#define FS_STAT_INDEX   128   // Requested Index would be outside of Index (used for Diagnostic)

// Flags for state_flag
#define STATE_DEEPSLEEP 1   // if set: JesFS in in deep sleep (JESFS_ERR_FS_SLEEPING)
#define STATE_POWERFAIL 2   // if set: Not enabled due to Power Fail (JESFS_ERR_VOLTAGE_TOO_LOW)
#define STATE_DEEPSLEEP_OR_POWERFAIL (STATE_DEEPSLEEP | STATE_POWERFAIL) // JESFS_ERR_FLASH_NOT_ACCESSIBLE

// Filedescriptor
typedef struct{
	uint32_t    _head_sadr;   // Hidden, head of file
	uint32_t    _wrk_sadr; // Hidden, working
	uint32_t    file_pos; // end pos is the current file len
	uint32_t    file_len;  // len after open (set by fs_open) FFFFFFFF if unknown
	uint32_t    file_crc32; // running CRC32 according ISO 3309, FFFFFFFF if not used (only with SF_OPEN_CRC)
	uint32_t    file_ctime; // Creation Time of the file UNIX-Time in seconds since 1.1.1970, 0:00:00

	uint16_t    _sadr_rel;   // Hidden, relative
	uint8_t     open_flags;  // current file flags (set by fs_open) OR  file_flags on disk OR (opt) SF_XOPEN_UNCLOSED
} FS_DESC;

// Statistic descriptor
typedef struct{
	char fname[FNAMELEN+1]; // Max. filenam len V1.0 (21+0x00)
	uint32_t file_ctime; // Creation Time of the file UNIX-Time in seconds since 1.1.1970, 0:00:00
	uint32_t file_len;
	uint32_t file_crc32; // CRC32 in Flash for this file, according ISO 3309, FFFFFFFF if not used (only with SF_OPEN_CRC)
	uint32_t _head_sadr;   // Hidden, head of file
	uint8_t  disk_flags;    // file flags on disk(written by fs_close) OR (opt) SF_XOPEN_UNCLOSED
} FS_STAT;

// Standard sizes for this implementation, see docu
#define SF_SECTOR_PH 4096   // SFlash-Physical Sector in Bytes
#define SF_BUFFER_SIZE_B   128 // 32 LONGS.

// Working Buf Flash
typedef union{
	uint8_t u8[SF_BUFFER_SIZE_B];
	uint16_t u16[SF_BUFFER_SIZE_B/2];
	uint32_t u32[SF_BUFFER_SIZE_B/4];
}  SF_BUFFER;


// Structure, describing the Flash
typedef struct{
    // -- Filled Interpret_ID --
    // e.g.
    // M25P40:     512kbB Manufacturer:20 Type:20 Density:13 ! ATTENTEION: No 4k-Ops, hence: not useabale for JesFs - > NotOK
    // MX25R8035:  1MByte Manufacturer:C2 Type:28 Density:14 -> Ok, tested
    // MX25R6435F: 8MByte Manufacturer:C2 Type:28 Density:17 -> Ok, tested
    // MT25QL128ABA 16Byte Manufacturer:20 Type:BA Density:18 -> Ok (should work)
    // etc..

    uint32_t identification;
    uint32_t total_flash_size;    // Max. available space. Here up to 2GB possible
    uint32_t creation_date; // UNIX-Time in seconds since 1.1.1970, 0:00:00 (Time when formated) 0x0xFFFFFFFF not allowed!

    // -- jesfs Header,
    // Init-Infos
    uint32_t lusect_adr;
    uint32_t available_disk_size;    // Available Disk space in Bytes (Marked 'todelete' or free)

    uint16_t files_used;    // Used Header Sectors
    uint16_t files_active;    // Active files (files_used - files_active = files_deleted)

#ifdef JSTAT
	 uint16_t sectors_todelete;  // Counts in 1, not really required, just for statistics
	 uint16_t sectors_clear;   // Counts in 1, not really required, just for statistics
	 uint16_t sectors_unknown;  // Counts in 1, Should be zero... Bot really required, just for statistics
#endif

    // Internal Buffer. Should be at least 64 Bytes. Default: 128 Bytes
    SF_BUFFER databuf;
    uint8_t state_flags;      // Currently only STATE_DEEPSLEEP used, 8Bit (at end of struct)
} SFLASH_INFO;

extern SFLASH_INFO sflash_info; //Describes Flash

// required by fs_sec1970_to_date()
typedef struct{ // Structure for a full date (readable)
	uint8_t sec;
	uint8_t min;
	uint8_t h;
	uint8_t d;
	uint8_t m;
	uint16_t a; // 1970 - 2099 (2100 is no leap year)
} FS_DATE;

//-------------------- HighLevel Functions --------------------------
int16_t fs_start(uint8_t mode);
int16_t fs_deepsleep(void);

int16_t fs_format(uint8_t fmode);
int32_t fs_read(FS_DESC *pdesc, uint8_t *pdest, uint32_t anz);
int16_t fs_rewind(FS_DESC *pdesc);
int16_t fs_open(FS_DESC *pdesc, char* pname, uint8_t flags);
int16_t fs_notexists(char *pname);
int16_t fs_write(FS_DESC *pdesc, uint8_t *pdata, uint32_t len);
int16_t fs_close(FS_DESC *pdesc);
int16_t fs_delete(FS_DESC *pdesc);
int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc);
uint32_t fs_get_crc32(FS_DESC *pdesc);

int16_t fs_info(FS_STAT *pstat, uint16_t fno);
void fs_sec1970_to_date(uint32_t asecs, FS_DATE *pd);
uint32_t fs_date2sec1970(FS_DATE *pd);
void fs_set_static_secs(uint32_t newsecs);

int16_t fs_check_disk(void cb_printf(char* fmt, ...), uint8_t *pline, uint32_t line_size);

#ifdef __cplusplus
}
#endif
//End
