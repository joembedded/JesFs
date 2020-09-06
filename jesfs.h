/*******************************************************************************
* JesFs.h: Header Files for JesFs
*
* JesFs - Jo's Embedded Serial File System
*
* Tested on 
* - Win
* - TI-RTOS CC13xx/CC26xx Launchpad 
* - nRF52840 on PCA10056 DK Board
*
* (C)2019 joembedded@gmail.com - www.joembedded.de
* Version: 
* 1.5 / 25.11.2019
* 1.6 / 22.12.2019 added fs_disk_check()
* 1.7 / 12.03.2020 added fs_date2sec1970()
*
*******************************************************************************/

/* List of Errors
-100: SPI Init (Hardware)
-101: Flash Timeout WaitBusy
-102: SPI Can not set WriteEnableBit (Flash locked?)
-103: ID:Unknown/illegal Flash Density (described the size)
-104: ID:Unknown Flash ID (eg. 0xC228 for Macronix M25xx, see docu)
-105: Illegal flash addr
-106: Block crosses sector border
-107: fs_start found problems in the filesystem structure (-> run recover)
-108: Unknown MAGIC, this Flash is either unformated or contains other data
-109: Flash-ID in the Flash Index does not match Hardware-ID (-> run recover)
-110: Filename to long/short
-111: Too many files, Index full! (ca. 1000 for 4k sectors)
-112: Sector border violated (before write)
-113: Flash full! No free sectors available or Flash not formatted
-114: Index corrupted (-> run recover) (or FS in deepsleep (see internal flag STATE_DEEPSLEEP))
-115: Number out of range Index (fs_stat)
-116: No active file at this entry (fs_stat)
-117: Illegal descriptor or file not open
-118: File not open for writing
-119: Index out of range
-120: Illegal sector address
-121: Short circle in sector list (-> run recover)
-122: sector list contains illegal file owner (-> run recover)
-123: Illegal sector type (-> run recover)
-124: File not found
-125: Illegal file flags (e.g. trying to delet a file opened for write)
-126: Illegal file system structure (-> run recover)
-127: Closed files can not be continued (for writing)
-128: Sector defect ('Header with owner') (-> run recover)
-129: File descriptor corrupted.
-130: Try to write to (unclosed) file in RAW with unknown end position
-131: Sector corrupted: Empty marked sector not empty
-132: File is empty
-133: Rename not possible with Files open as READ or RAW
-134: Rename requires an empty File as new Filename
-135: Both files must be open for Rename
-136: Erase Sector failed
-137: Write to Flash Failed
-138: Verify Failed
-138: Voltage too low for Write/Erase
-139: Format parameter
-140: Command Deepsleep: Filesystem already sleeping (only informative)
-141: Other Commands: Filesystem sleeping!
 */

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

// Define this macro for additioinal statistics
#define JSTAT 

// Sample-Flash ID MACRONIX (Ultra-Low-Power), add others
#define MACRONIX_MANU_TYP    0xC228  // Macronix MX25R-Low-Power-Series first 2 ID-Bytes (without Density)
#define GIGADEV_MANU_TYP     0xC840  // GigaDevices (used in RC1310F) first 2 ID-Bytes (without Density)

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
#define STATE_DEEPSLEEP 1   // if set: JesFS in in deep sleep

// Filedescriptor
typedef struct{
	uint32_t    _head_sadr;   // Hidden, head of file
	uint32_t    _wrk_sadr; // Hidden, working
	uint32_t    file_pos; // end pos is the current file len
	uint32_t    file_len;  // len after open (set by fs_open)
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
	uint8_t  disk_flags;    // file flags on disk´(written by fs_close) OR (opt) SF_XOPEN_UNCLOSED
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
    // MX25R6435F: 8MByte Manufacturer:C2 Type:28 Density:17 -> Ok (should work)
    // MT25QL128ABA 16Byte Manufacturer:20 Type:BA Density:18 -> Ok (should work)
    // etc..

    uint32_t identification;
    uint32_t total_flash_size;    // Max. available space. Here up to 2GB possible
	uint32_t creation_date; // UNIX-Time in seconds since 1.1.1970, 0:00:00 (Time when formated) 0x0xFFFFFFFF not allowed!

    // -- jesfs Header,
    // Init-Infos
    uint32_t lusect_adr;
    uint32_t available_disk_size;    // Available Disk space in Bytes ('todelete' or echt free)

    uint16_t files_used;    // Used Header Sectors
    uint16_t files_active;    // Active files (Used-Active=Deleted)

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
int16_t fs_write(FS_DESC *pdesc, uint8_t *pdata, uint32_t len);
int16_t fs_close(FS_DESC *pdesc);
int16_t fs_delete(FS_DESC *pdesc);
int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc);
uint32_t fs_get_crc32(FS_DESC *pdesc);

int16_t fs_info(FS_STAT *pstat, uint16_t fno);
void fs_sec1970_to_date(uint32_t asecs, FS_DATE *pd);
uint32_t fs_date2sec1970(FS_DATE *pd);

int16_t fs_check_disk(void cb_printf(char* fmt, ...), uint8_t *pline, uint32_t line_size);


#ifdef __cplusplus
}
#endif
//End
