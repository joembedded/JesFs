/*******************************************************************************
* JesFs_ml.c: JesFs MediumLevel(Serial Flash Management) driver
*
* JesFs - Jo's Embedded Serial File System
*
* (C)2019 joembedded@gmail.com - www.joembedded.de
* Version: 1.5 / 25.11.2019
*
*******************************************************************************/

//#include <stdio.h>

/* Required Std. headers */
#include <stdint.h>
#include <stddef.h>

#include "jesfs.h"
#include "jesfs_int.h"

SFLASH_INFO sflash_info; // Describes the Flash

//------------------- MediumLevel SPI Start ------------------------
//* Send SPUI Singlebyte-Command. More might follow
void sflash_bytecmd(uint8_t cmd, uint8_t more){
	sflash_select();
	sflash_spi_write(&cmd, 1);
	if(!more) sflash_deselect();
}


/* READ IDENTIFICATION
 * Flash must be woen up before, else ID is 0 for (most) flash*/
#define CMD_RDID    0x9F    // Read Identification: Manuf.8 Type.8 Density.8
uint32_t sflash_QuickScanIdentification(void){
	uint32_t id;
	uint8_t buf[3];
	sflash_bytecmd(CMD_RDID,1); // More
	sflash_spi_read(buf, 3);
	sflash_deselect();
	// Build 32Bit id safely, so it will work also on 16 Bit compilers (like MSP430)
	id=buf[0]; id<<=8; // Manufacturer
	id|=buf[1]; id<<=8; // Type
	id|=buf[2]; // Density
	return id;
}

/* Analyse ID of the flash */
int16_t sflash_interpret_id(uint32_t id){
	uint8_t h;
	sflash_info.total_flash_size=0;
	sflash_info.identification=id;

	switch(id>>8){   // Check Without Density
	default:
		return -104;    // Unknown Type (!!: e.g. Micron has otherTypes th. Macronix, but identical Fkts (Quiescent Current for Macronix is the lowest..)

	// List of tested/knownAsGood Flash-Manufacturer/IDs (see Header File).  Others may be added later
	case MACRONIX_MANU_TYP: // The 1MB-Version is on the TI CC1310 Launchpad, 2-16 MB on LTraX, ..
        case GIGADEV_MANU_TYP: // The 2MB-Version is inside Radiocontrolli's CC1310F

	    // ...add others...
		break;      // OK!
	}

#ifdef DEBUG_FORCE_MINIDISK_DENSITY
	h=DEBUG_FORCE_MINIDISK_DENSITY; // MiniDisc >= 2 Sectors Minimum
#else
	h=id&255;   // Density
	if(h<MIN_DENSITY || h>MAX_DENSITY) return -103;  // Unknown Density! 8*512kB-8*16MB ist OK
#endif
	sflash_info.total_flash_size=1<<(h);     // All OK for JesFs: 8k-16MB OK fuer 3B-SPI Flash, opt. 2GB
	return 0;   // OK
}

/* DEEP POWER DOWN
 * Flash Deep Sleep. Wake RELASE or (some) by READ IDENTIFIATION */
#define CMD_DEEPPOWERDOWN 0xB9
void sflash_DeepPowerDown(void){
	sflash_bytecmd(CMD_DEEPPOWERDOWN,0);   // NoMore
}

/* RELEASE from DEEP POWER DOWN
 * older Types of  Flash give ID, newer; Omly WaleUp. After tPowerUp read ID
 * Very important: e.e. MX25R80535 requires ca. 35usec until ready!
 * Unbedingt beruecksichtigen (separat, User) */
#define CMD_RELEASEDPD 0xAB
void sflash_ReleaseFromDeepPowerDown(void){
	sflash_bytecmd(CMD_RELEASEDPD,0);   // NoMore
	// sflash_wait_usec(50);  // up to user to regards that!
}

/* Read Len Bytes FlashAdr(sdr) to sbuf
 * For Adr>=16MB: use 4-Byte-CMD  (0x13), extend Driver to 32 Bit!!!
 */
#define CMD_READDATA 0x03
void sflash_read(uint32_t sadr, uint8_t* sbuf, uint16_t len){
	uint8_t buf[4];   //
	buf[0]=CMD_READDATA;
	buf[1]=(uint8_t)(sadr>>16);
	buf[2]=(uint8_t)(sadr>>8);
	buf[3]=(uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_spi_read(sbuf, len);
	sflash_deselect();
}

/* ReadStatusRegister Bit0: 1:WriteInprogress Bit1: WriteEnabled, Restbits ign. */
#define CMD_STATUSREG 0x05
uint8_t sflash_ReadStatusReg(void){
	uint8_t buf;
	sflash_bytecmd(CMD_STATUSREG,1);   // More
	sflash_spi_read(&buf, 1);
	sflash_deselect();
	return buf;
}
/* Write Enable */
#define CMD_WRITEENABLE 0x06
void sflash_WriteEnable(void){
	sflash_bytecmd(CMD_WRITEENABLE,0);   // NoMore
}

/* Len Bytes write FlashAdr(sdr) to sbuf.
 * For Adr>=16MB: 4-Byte-CMD  (0x12), separate!
 * Before: Enable set, autom. reset to 0
 * ca. 0.8-4 msec/pagewrite
 */
#define CMD_PAGEWRITE 0x02
void sflash_PageWrite(uint32_t sadr, uint8_t* sbuf, uint16_t len){
	uint8_t buf[4];   //
	buf[0]=CMD_PAGEWRITE;
	buf[1]=(uint8_t)(sadr>>16);
	buf[2]=(uint8_t)(sadr>>8);
	buf[3]=(uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_spi_write(sbuf, len);
	sflash_deselect();
}

/* Start BULK Erase
 * (Write Enable before, takes long! Scan Status)
 */
#define CMD_BULKERASE 0xC7
void sflash_BulkErase(void){
	sflash_bytecmd(CMD_BULKERASE,0);   // NoMore
}

/* Sector Erase 4k
 * Attention: 4k not on all flash, bust almost all.
 * M25P40: -
 * MX25R8035:  100 typ / 300 max msec
 * MT25QL128   50 typ / 400 max msec
 */
#define CMD_SECTOR4K_ERASE 0x20
void sflash_llSectorErase4k(uint32_t sadr){
	uint8_t buf[4];  //
	buf[0]=CMD_SECTOR4K_ERASE;
	buf[1]=(uint8_t)(sadr>>16);
	buf[2]=(uint8_t)(sadr>>8);
	buf[3]=(uint8_t)(sadr);
	sflash_select();
	sflash_spi_write(buf, 4);
	sflash_deselect();
}

// x msec lang warten bis Flash fertig oder Fehler
int16_t sflash_WaitBusy(uint32_t msec){
	while(msec--){
		sflash_wait_usec(1000);
		if(!(sflash_ReadStatusReg()&1)) return 0; // OK
	}
	return -101;  // Fehler! Timout
}

// WriteEnabled setzen und checken ob OK (lt. DB) oder Fehler(-1)
int16_t sflash_WaitWriteEnabled(void){
    // if(m_voltage()< MIN_VDD_SFLASH) return -1;  // Voltage too low! Systemabhaengig
	sflash_WriteEnable();
	if(sflash_ReadStatusReg()&2) return 0;
	return -102;  // Fehler! Flash locked? oder WP
}


/* Write Sector up to maximum. Write in Pages. Attention: Pageprog keeps the SFlash busy for a few mesec. Theoretically
 * the check yould be retarded for better performance, but thie makes the software more difficult. Maybe in a later version.
 */
int16_t sflash_SectorWrite(uint32_t sflash_adr, uint8_t* sbuf, uint32_t len){
	uint32_t maxwrite;

	if(sflash_adr>=sflash_info.total_flash_size) return -105; // Flash Full! Illegal Address
	maxwrite=SF_SECTOR_PH-(sflash_adr&(SF_SECTOR_PH-1)); 
	if(len>maxwrite) return -106;   // Sektorviolation

	while(len){
		maxwrite=256-(uint8_t)sflash_adr;
#ifdef SF_TX_TRANSFER_LIMIT
		if(maxwrite>SF_TX_TRANSFER_LIMIT) maxwrite=SF_TX_TRANSFER_LIMIT;
#endif
		if(len<maxwrite) maxwrite=len;    // Dann halt weniger in diesem Block schreiben
		if(sflash_WaitWriteEnabled()) return -102; // Wait unt. OK, Fehler 1:1
		sflash_PageWrite(sflash_adr, sbuf, maxwrite);
		if(sflash_WaitBusy(100)) return -101; // 100 msec unt. Page OK
		sbuf+=maxwrite;
		sflash_adr+=maxwrite;
		len-=maxwrite;
	}
	return 0;   // Alles OK
}

// HighLevel SectorErase (inc. Error Check and
int16_t sflash_SectorErase(uint32_t sadr){
	if(sflash_WaitWriteEnabled()) return -102; 
	sflash_llSectorErase4k(sadr);
	if(sflash_WaitBusy(400)) return -101; // 400 msec max page
	return 0;
}
//------------------- MediumLevel SPI OK ------------------------
