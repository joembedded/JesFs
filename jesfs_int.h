/*******************************************************************************
* JesFs_int.h - internal headers
*
* JesFs - Jo's Embedded Serial File System
*
* The following parts are only relevant for
* the (internal) medium/low level drivers
*
* (C) joembedded@gmail.com - www.joembedded.de
* Version: see Header Files
*
*******************************************************************************/

#ifdef __cplusplus
extern "C"{
#endif
//------------------- Internal JesFS Defines ------------------------

// For Debugging/Stress tests: If defined, treat SPI as very small disk. Size must be Sectorsize^x and
// at least 2 Sectors (=8kB) Maximum is 2GB, for 3-Byte SPI-Flash: Maximum 16MB
// #define DEBUG_FORCE_MINIDISK_DENSITY 0x0F // (1<<x), but >= 2 Sectors, here 0xF: 32kB = 8 Sectors

#define MIN_DENSITY 0x0D   	// 0x13: 512k, smaller doesn not make sense, except for tests, 0x0D is the minimum
#define MAX_DENSITY 0x18    // 0x18: 16 MB is the maximum for 3-byte addressed Flash. (But up to 2GB is possible)


// Header of each sector
#define HEADER_SIZE_L   3  // equals 12 Bytes
#define HEADER_SIZE_B   (HEADER_SIZE_L*4)
// addition header at start of file
#define FINFO_SIZE_B    36

//------------------- Internal JesFS Functions ------------------------

#define HEADER_MAGIC                0x4673654A   //'JesF'
#define SECTOR_MAGIC_HEAD_ACTIVE    0xFFFF293A
#define SECTOR_MAGIC_HEAD_DELETED   0xFFFF2130
#define SECTOR_MAGIC_DATA           0xFFFF5D5B
#define SECTOR_MAGIC_TODELETE       0xFFFF4040

// Easy Access B W L
typedef union{
	uint8_t     u8[4];
	uint16_t    u16[2];
	uint32_t    u32;
} SF_LONGWORDBYTE;  // Structure for byte/word/long access

//------------------- LowLevel SPI Functions Depending on Hardware! -------
void sflash_wait_usec(uint32_t usec);

int16_t sflash_spi_init(void);
void sflash_spi_close(void);

void sflash_select(void);
void sflash_deselect(void);
void sflash_spi_read(uint8_t *buf, uint16_t len); // len is 16Bit!
void sflash_spi_write(const uint8_t *buf, uint16_t len); // len is 16Bit!

//---------------- Little helpers for MediumLevel ------------------
uint32_t fs_strlen(char *s);
void fs_strncpy(char *d, char *s, uint8_t maxchar);
void fs_memset(uint8_t *p, uint8_t v, uint32_t n);
int16_t fs_strcmp(char *s1, char *s2);
uint32_t fs_track_crc32(uint8_t *pdata, uint32_t wlen, uint32_t crc_run);
uint32_t fs_get_secs(void); // Unix-Secs

//------------------- MediumLevel SPI Functions ------------------------
void sflash_bytecmd(uint8_t cmd, uint8_t more);
uint32_t sflash_QuickScanIdentification(void);
int16_t sflash_interpret_id(uint32_t id);
void sflash_DeepPowerDown(void);
void sflash_ReleaseFromDeepPowerDown(void);
void sflash_read(uint32_t sadr, uint8_t* sbuf, uint16_t len);
uint8_t sflash_ReadStatusReg(void);
void sflash_WriteEnable(void);
void sflash_PageWrite(uint32_t sadr, uint8_t* sbuf, uint16_t len);
void sflash_BulkErase(void);
void sflash_llSectorErase4k(uint32_t sadr); // LowLevel, bezogen auf 4K
int16_t sflash_WaitBusy(uint32_t msec);
int16_t sflash_WaitWriteEnabled(void);
int16_t sflash_SectorWrite(uint32_t sflash_adr, uint8_t* sbuf, uint32_t len);
int16_t sflash_SectorErase(uint32_t sadr); // High-Level
#ifdef __cplusplus
}
#endif
//--------------End of Internal JesFS Functions ------------------------
