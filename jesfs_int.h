/*******************************************************************************
* JesFs_int.h
*
* The following parts are only relevant for
* the (internal) medium/low level drivers
*
* (C) joembedded@gmail.com 2018
* Please regard: This is Copyrighted Software!
* It may be used for education or non-commercial use, but without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'
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

// Standard-Groesse, auf den SW in dieser Implmenentierung angepasst ist, see Docu
#define SF_SECTOR_PH 4096   // SFlash-Physical Sektor in Bytes

// Header eines jede Sektors
#define HEADER_SIZE_L   3  // alo 12 Bytes
#define HEADER_SIZE_B   (HEADER_SIZE_L*4)
// Header am Dateianfang
#define FINFO_SIZE_B    36

//------------------- Internal JesFS Functions ------------------------

/* HEADER eines jeden Sektors:
* MAGIC/USAGE - USAGE
* OWNER
* NEXT
*
* Aufbau Header SektorenN und Sekto0
* USAGE: 0: Zum Loeschen freigegeben, FFFFFFFF: komplett frei, ansonsten belegt
*/

/* Header am Dateianfang (nur in Sektoren SECTOR_MAGIC_HEAD_xxx)
 * LEN     Nach Anlage FFFFFFFF, wird erst ggf. bei CLOSE geschrieben
 * CRC32   Nach Anlage FFFFFFFF
 * DATEINAME[FNAMELEN]
 * FLAGS_OPEN         Flags bei Erstellung
 * FLAGS_MANAGEMENT   Verwaltungsflags (init mit FF)
 * (Danach die Dateidaten...)
 */

#define HEADER_MAGIC 				0x4673654A   //'JesF'
#define SECTOR_MAGIC_HEAD_ACTIVE    0xFFFF293A  //':)': Belegter Sektor (Dateianfang AKTIV), 0; ToDelete, FFFFFFFF: Frei
#define SECTOR_MAGIC_HEAD_DELETED   0xFFFF2130  //'0!': Belegter Sektor (Dateianfang inaktiv)
#define SECTOR_MAGIC_DATA           0xFFFF5D5B  //'[]': Belegter Sektor (Daten)
#define SECTOR_MAGIC_TODELETE       0xFFFF4040  // '@@' Freigegebener Sektor (Daten)

// Zum Verwalten
typedef union{
	uint8_t     u8[4];
	uint16_t    u16[2];
	uint32_t    u32;
} SF_LONGWORDBYTE;  // Structure for byte/word/long access

#define SF_BUFFER_SIZE_B   128 // 32 LONGS
// Puffer fuers Flash
typedef union{
	uint8_t u8[SF_BUFFER_SIZE_B];
	uint16_t u16[SF_BUFFER_SIZE_B/2];
	uint32_t u32[SF_BUFFER_SIZE_B/4];
}  SF_BUFFER;


// Struktur, die das Flash beschreibt.
typedef struct{
	// -- Ausgefuellt von Interpret_ID --
	// z.B.
	// M25P40:     512kbB Manufacturer:20 Type:20 Density:13 ! Achtung keine 4k-Ops, daher hier NICHT verwendbar - > NotOK
	// MX25R8035:  1MByte Manufacturer:C2 Type:28 Density:14 -> Ok, tested
	// MX25R6435F: 8MByte Manufacturer:C2 Type:28 Density:17 -> Ok (should work)
	// MT25QL128ABA 16Byte Manufacturer:20 Type:BA Density:18 -> Ok (should work)
	// etc..

	uint32_t identification;
	uint32_t total_flash_size;    // Maximal verfuegbarer Platz. Hier bis 2GB verwaltbar.

	// -- jesfs Header,
	// Init-Infos
	uint32_t lusect_adr;    // Adresse des letzten verwendetn Sektors, der naechste freie kommt danach
	uint32_t available_disk_size;    // Soviel Platz ist auf Dist noch frei (todelete oder echt free)

	uint16_t files_used;    // Zaehlt in 1 Ben. bei open
	uint16_t files_active;    // Zaehlt in 1, rein informativ

	uint16_t sectors_todelete;  // Zaehlt in 1, rein informativ
	uint16_t sectors_clear; // Zaehlt in 1, rein informativ
	uint16_t sectors_unknown;  // Zaehlt in 1, rein informativ

	// Puffer fuer Sektorn die ersten HEADER_SIZE_L Longs sind reserviert fuer den gerade bearbeiteten Sektor
	SF_BUFFER databuf;     // mind. so gross, dass Dateianfang reinpasst
} SFLASH_INFO;

extern SFLASH_INFO sflash_info; // Beschreibt das Flash

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
void fs_strcpy(char *d, char *s);
void fs_memset(uint8_t *p, uint8_t v, uint32_t n);
int16_t fs_strcmp(char *s1, char *s2);
uint32_t fs_track_crc32(uint8_t *pdata, uint32_t wlen, uint32_t crc_run);

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
