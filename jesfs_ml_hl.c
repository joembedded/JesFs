/*******************************************************************************
* JesFs_ml_hl.c: JesFs HighLevel(User) and MidLevel(SPI Management) drivers
*
* JesFs - Jo's Embedded Serial File System
* Tested on Win and TI-RTOS CC1310 Launchpad
*
* Remark: One of the most important design topics for JesFs was robustness.
* So more checks are included than theoreticaly necessary.
*
*
* (C)2018 joembedded@gmail.com - www.joembedded.de
*
* --------------------------------------------
* Please regard: This is Copyrighted Software!
* --------------------------------------------
*
* It may be used for education or non-commercial use, and without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'.
*******************************************************************************/

//#include <stdio.h>
//#define my_printf printf

/* Required Std. headers */
#include <stdint.h>
#include <stddef.h>

//----------------------------------------------- JESFS-START ----------------------
#include "jesfs.h"
#include "jesfs_int.h"

SFLASH_INFO sflash_info; // Describes the Flash

// Driver designed for 4k-Flash - JesFs
#if SF_SECTOR_PH != 4096
 #error "Phyiscal Sector Size SPIFlash must be 4K"
#endif
#if FNAMELEN!=25
 #error "FNAMELEN fixed to 25+Zero-Byte by Design"
#endif

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
	case MACRONIX_MANU_TYP: // The 1MB-Version is on the CC1310 Launchpad
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
 * For Adr>=16MB: use 4-Byte-CMD  (0x13), separate Driver!!!
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

//------------------- HighLevel FS Start ------------------------
// May omit unnecessary libs, slightly modified to standard
uint32_t fs_strlen(char *s){
	char *p = s;
	while (*p) p++;
	return p - s;
}
void fs_strcpy(char *d, char *s){
	while(*s) *d++=*s++;
	*d=0;
}
void fs_memset(uint8_t *p, uint8_t v, uint32_t n){
	while(n--) *p++ = v;
}
int16_t fs_strcmp(char *s1, char *s2){ // Only required for equal(0) or !equal(!0)
	for(;;){
		if(*s1==0 || *s1!=*s2) return *s1-*s2;
		s1++;
		s2++;
	}
}

#define POLY32  0xEDB88320    // ISO 3309
uint32_t fs_track_crc32(uint8_t *pdata, uint32_t wlen, uint32_t crc_run){
	uint8_t j;
	while(wlen--){
	crc_run ^= *pdata++;
		for (j = 0; j < 8; j++){
			if (crc_run & 1) crc_run = (crc_run >> 1) ^ POLY32;
			else    crc_run =  crc_run >> 1;
		}
	}
	return crc_run;
}

int16_t sflash_sadr_invalid(uint32_t sadr){
	if(sadr==0xFFFFFFFF) return 0;  // OK
	if(!sadr) return -1;
	if(sadr & (SF_SECTOR_PH-1)) return -2; // FATAL
	if(sadr>=sflash_info.total_flash_size) return -3; // FATAL
	return 0; // Ok
}

int16_t flash_set2delete(uint32_t sadr){
	int16_t res;
	uint32_t thdr[3];
	uint32_t max_sect;
	uint32_t oadr;
	oadr=sadr;
	max_sect=(sflash_info.total_flash_size/SF_SECTOR_PH);
	while(--max_sect){
		if(sflash_sadr_invalid(sadr)) return -120;
		sflash_read(sadr,(uint8_t*)thdr,12);
		if(thdr[0]==SECTOR_MAGIC_HEAD_ACTIVE){
			if(thdr[1]!=0xFFFFFFFF) return -122;
			thdr[0]=SECTOR_MAGIC_HEAD_DELETED;
			sflash_info.files_active--;
		}else if(thdr[0]==SECTOR_MAGIC_DATA){
			if(thdr[1]!=oadr) return -122;
			thdr[0]=SECTOR_MAGIC_TODELETE;
			sflash_info.available_disk_size+=SF_SECTOR_PH;
		}else return -123;  // Illegal
		res= sflash_SectorWrite(sadr,(uint8_t*)thdr, 4);
		if(res) return res;
		sadr=thdr[2];
		if(sadr==0xFFFFFFFF) return 0; 
	}
	return -121;
}

uint16_t sflash_find_mlen(uint32_t sadr,uint16_t max_sec_rd){
	uint16_t wlen;
	uint16_t used_len=max_sec_rd;
	sadr+=max_sec_rd;
	while(max_sec_rd){
		wlen=max_sec_rd;
		if(wlen>SF_BUFFER_SIZE_B) wlen=SF_BUFFER_SIZE_B;
		max_sec_rd-=wlen;
		sadr-=wlen;
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,wlen);
		while(wlen--){
			if(sflash_info.databuf.u8[wlen]!=0xFF) return used_len;
			used_len--;
		}
	}
	return 0;
}

// Copy IntraFlash and Page-Safe
int16_t flash_intrasec_copy(uint32_t sadr, uint32_t dadr, uint16_t clen){
    int16_t res;
    uint16_t blen;
    while(clen){
        blen=clen;
        if(blen>SF_BUFFER_SIZE_B) blen=SF_BUFFER_SIZE_B;

        sflash_read(sadr,(uint8_t*)&sflash_info.databuf ,blen);
        res=sflash_SectorWrite(dadr, (uint8_t*)&sflash_info.databuf, blen);
        if(res) return res;
        sadr+=blen;
        dadr+=blen;
        clen-=blen;
    }
    return 0;
}

//--------------------------- Frm here User Functions ----------------------------------------

/* Start Filesystem - Fill structurs and check basic parameters */
int16_t fs_start(uint8_t mode){
	int16_t res;
	uint32_t id;
	uint32_t sadr;
	uint32_t idx_adr;
	uint32_t dir_typ;
	uint16_t err;

	res=sflash_spi_init();
	if(res) return res;        // Error 2 User

	// Flash wakeup 
	sflash_ReleaseFromDeepPowerDown();
	sflash_wait_usec(45);

	// ID read and get setup
	id=sflash_QuickScanIdentification();

	if(mode&FS_START_RESTART){
		if(sflash_info.total_flash_size && id==sflash_info.identification){
			return 0;   // Wake only
		}
	}

	res=sflash_interpret_id(id);
	if(res) return res;

	// OK, Flash is known
	sflash_read(0,(uint8_t*)&sflash_info.databuf ,HEADER_SIZE_B); 

	if(sflash_info.databuf.u32[0]!=HEADER_MAGIC) return -108;
	if(sflash_info.databuf.u32[1]!=sflash_info.identification) return -109;
	// sflash_info.databuf.u32[2] // Bulk/ID, User/ID not used in V1.0

	err=0;
	sflash_info.available_disk_size=sflash_info.total_flash_size-SF_SECTOR_PH;
	sflash_info.sectors_todelete=0;
	sflash_info.sectors_clear=0;
	sflash_info.sectors_unknown=0;
	sflash_info.files_used=0;
	sflash_info.files_active=0;

	sflash_info.lusect_adr=0;  
	// Scan  Takes on 1M-Flash 12msec, 16M-Flash: 200msec (12 MHz SPI)
	for(sadr=SF_SECTOR_PH;sadr<sflash_info.total_flash_size;sadr+=SF_SECTOR_PH){
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,(mode&FS_START_FAST)?4:12);
		switch(sflash_info.databuf.u32[0]){
		case 0xFFFFFFFF:
			sflash_info.sectors_clear++;
			break;
		case SECTOR_MAGIC_TODELETE:
			sflash_info.sectors_todelete++;
			sflash_info.lusect_adr=sadr;
			break;

		case SECTOR_MAGIC_HEAD_ACTIVE:
			sflash_info.files_active++;
			sflash_info.lusect_adr=sadr;
		case SECTOR_MAGIC_HEAD_DELETED:
			sflash_info.files_used++;
			sflash_info.lusect_adr=sadr;

		case SECTOR_MAGIC_DATA:
			sflash_info.available_disk_size-=SF_SECTOR_PH;
			sflash_info.lusect_adr=sadr;
			break;

		default:
			sflash_info.sectors_unknown++; // !!! Big Failure
			err++;
		}
		
		if(!(mode&FS_START_FAST)){
			switch(sflash_info.databuf.u32[0]){
			case 0xFFFFFFFF:
				if(sflash_info.databuf.u32[1]!=0xFFFFFFFF || sflash_info.databuf.u32[2]!=0xFFFFFFFF) err++;
				break;
			case SECTOR_MAGIC_HEAD_ACTIVE:
			case SECTOR_MAGIC_HEAD_DELETED:
				if(sflash_info.databuf.u32[1]!=0xFFFFFFFF) err++;
				if(sflash_sadr_invalid(sflash_info.databuf.u32[2])) err++;
				break;
			case SECTOR_MAGIC_DATA:
			case SECTOR_MAGIC_TODELETE:
				idx_adr=sflash_info.databuf.u32[1];
				if(idx_adr==0xFFFFFFFF || sflash_sadr_invalid(idx_adr)) err++;
				if(sflash_sadr_invalid(sflash_info.databuf.u32[2])) err++;
				break;
			}
		}
	}
	
	sadr=HEADER_SIZE_B;
	id=0;
	while(sadr!= SF_SECTOR_PH){
		sflash_read(sadr,(uint8_t*)&idx_adr,4); 
		if(idx_adr==0xFFFFFFFF) break; 
		else{
			if(sflash_sadr_invalid(idx_adr) ) err++;
			else{
				sflash_read(idx_adr,(uint8_t*)&dir_typ,4);
				if(dir_typ==SECTOR_MAGIC_HEAD_ACTIVE || dir_typ==SECTOR_MAGIC_HEAD_DELETED) id++;
				else err++;
			}
		}
		sadr+=4;
	}

	if(err || (uint16_t)id!=sflash_info.files_used ) return -107; // Corrupt Data?
	return 0;   // OK
}

/* Set Flash to Ultra-Low-Power mode. Call fs_start(FS_RESTART) to continue/wake */
void  fs_deepsleep(void){
	sflash_DeepPowerDown();
}

/* Format Filesystem. May require between 30-120 seconds (even more, see Datasheet) for a 512k-16 MB Flash) */
int16_t fs_format(uint32_t f_id){
	uint32_t sbuf[3];   
	int16_t res;

	if(sflash_WaitWriteEnabled()) return -102; // Wait enabled until OK, Fehler 1:1
	sflash_BulkErase();
	if(sflash_WaitBusy(120000)) return -101; // 100 msec evtl. old page, Fehler 1:1

	sbuf[0]=HEADER_MAGIC;
	sbuf[1]=sflash_info.identification;
	sbuf[2]=f_id;

	res=sflash_SectorWrite(0, (uint8_t*)sbuf, 12);    // Header V1.0
	if(res) return res;

	return fs_start(FS_START_NORMAL);
}


uint32_t sflash_get_free_sector(void){
	uint32_t thdr;  
	uint32_t max_sect;
	// Some embedded compilers complain about the Division. In fact, it will result in a shift. So it might be ignored
	max_sect=(sflash_info.total_flash_size/SF_SECTOR_PH);
	while(--max_sect){
		sflash_info.lusect_adr+=SF_SECTOR_PH;
		if(sflash_info.lusect_adr>=sflash_info.total_flash_size) sflash_info.lusect_adr=SF_SECTOR_PH;
		sflash_read(sflash_info.lusect_adr,(uint8_t*)&thdr,4);

		if(thdr==SECTOR_MAGIC_TODELETE || thdr==0xFFFFFFFF){
			if(thdr==SECTOR_MAGIC_TODELETE){ 
				if(sflash_SectorErase(sflash_info.lusect_adr)) return 0; 
			}
			return sflash_info.lusect_adr;
		}
	}
	return 0;   
}

// --- fs_read() ---
int32_t fs_read(FS_DESC *pdesc, uint8_t *pdest, uint32_t anz){
	uint32_t h;
	uint32_t next_sect;
	int32_t total_rd=0;     // max 2GB
	uint16_t max_sec_rd;
	uint16_t uc_mlen;

	if(!pdesc->_head_sadr) return -117;

	while(anz){
		sflash_read(pdesc->_wrk_sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B);
		h=sflash_info.databuf.u32[0];
		if(h==SECTOR_MAGIC_HEAD_ACTIVE){
			if(sflash_info.databuf.u32[1]!=0xFFFFFFFF) return -128;
		}else if(h==SECTOR_MAGIC_DATA){
			if(sflash_info.databuf.u32[1]!=pdesc->_head_sadr) return -122;
		}else return -123;

		next_sect=sflash_info.databuf.u32[2];
		if(sflash_sadr_invalid(next_sect)) return -120;

		while(anz){
			max_sec_rd=(SF_SECTOR_PH-pdesc->_sadr_rel);
			if(max_sec_rd>(SF_SECTOR_PH-HEADER_SIZE_B)) return -129;

			if(pdesc->file_len!=0xFFFFFFFF) {
				h=pdesc->file_len-pdesc->file_pos;
				if(anz>h) anz=h;

			}else if(next_sect==0xFFFFFFFF){
				uc_mlen=sflash_find_mlen(pdesc->_wrk_sadr+pdesc->_sadr_rel,max_sec_rd);
				pdesc->file_len=pdesc->file_pos+uc_mlen;   // Now we know the End
				if(anz>(uint32_t)uc_mlen) anz=uc_mlen;
			}

#ifdef SF_RD_TRANSFER_LIMIT
			if(pdest && max_sec_rd>SF_RD_TRANSFER_LIMIT) max_sec_rd=SF_RD_TRANSFER_LIMIT;
#endif

			if((uint32_t)max_sec_rd>anz) max_sec_rd=anz;
			if(pdest){
				sflash_read(pdesc->_wrk_sadr+pdesc->_sadr_rel,pdest,max_sec_rd);
				if(pdesc->open_flags & SF_OPEN_CRC) pdesc->file_crc32 = fs_track_crc32(pdest, max_sec_rd, pdesc->file_crc32);
				pdest+=max_sec_rd;
			}

			anz-=max_sec_rd;
			pdesc->file_pos+=max_sec_rd;
			pdesc->_sadr_rel+=max_sec_rd;
			total_rd+=max_sec_rd;
			if(pdesc->_sadr_rel==SF_SECTOR_PH){
				if(next_sect!=0xFFFFFFFF){
					pdesc->_wrk_sadr=next_sect;
					pdesc->_sadr_rel=HEADER_SIZE_B;
				}
				break;
			}
		}
	}
	return total_rd; // max 2GB
}

/* Rewind File to Start */
int16_t fs_rewind(FS_DESC *pdesc){
	if(!pdesc->_head_sadr) return -117;
	if(pdesc->open_flags & SF_OPEN_WRITE) return -118;
	pdesc->_wrk_sadr=pdesc->_head_sadr;
	pdesc->file_pos=0;
	pdesc->_sadr_rel=HEADER_SIZE_B+FINFO_SIZE_B;
	pdesc->file_crc32=0xFFFFFFFF; // Reset CRC
	return 0;
}


/* Open File, if flag GENERATE is set, it will be generated, if already exists, it will be deleted */
int16_t fs_open(FS_DESC *pdesc, char* pname, uint8_t flags){
	int16_t res;
	uint16_t i;
	uint32_t sadr=0;
	uint32_t sfun_adr=0;

	pdesc->_head_sadr=0;
	pdesc->file_crc32=0xFFFFFFFF;

	if(!*pname || fs_strlen(pname)>FNAMELEN) return -110;

	for(i=0;i<sflash_info.files_used;i++){
		sflash_read(HEADER_SIZE_B+i*4,(uint8_t*)&sadr,4);
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B);
		if(sflash_info.databuf.u32[0]==SECTOR_MAGIC_HEAD_DELETED){
			sfun_adr=sadr;
		}else if(sflash_info.databuf.u32[0]!=SECTOR_MAGIC_HEAD_ACTIVE) return -114;
		else if(!fs_strcmp(pname,(char*)&sflash_info.databuf.u8[HEADER_SIZE_B+8])){
			break;
		}
		sadr=0;
	}
	pdesc->open_flags=flags;
	pdesc->_sadr_rel=HEADER_SIZE_B+FINFO_SIZE_B;
	pdesc->file_pos=0;
	pdesc->file_len=0;

	if(sadr){
		pdesc->_head_sadr=sadr;
		pdesc->_wrk_sadr=sadr;
		pdesc->file_len=sflash_info.databuf.u32[HEADER_SIZE_L+0];
		if(flags & (SF_OPEN_READ | SF_OPEN_RAW)) return 0;
		res=flash_set2delete(sadr);
		if(res) return res;
		sfun_adr=sadr;
	}else{
		if(!(flags & SF_OPEN_CREATE)) return -124;
	}

	if(!sfun_adr){
		sfun_adr=sflash_get_free_sector();
		if(!sfun_adr) return -113;
		if(HEADER_SIZE_B+sflash_info.files_used*4>=(SF_SECTOR_PH-4)) return -111;
		res=sflash_SectorWrite(HEADER_SIZE_B+sflash_info.files_used*4,(uint8_t*)&sfun_adr, 4);
		if(res) return res;
		sflash_info.available_disk_size-=SF_SECTOR_PH;
		sflash_info.files_used++;
	}else{
		res=sflash_SectorErase(sfun_adr);
		if(res) return res;
	}

	fs_memset((uint8_t*)&sflash_info.databuf,0xFF,HEADER_SIZE_B+FINFO_SIZE_B);
	sflash_info.databuf.u32[0]=SECTOR_MAGIC_HEAD_ACTIVE;
	fs_strcpy((char*)&sflash_info.databuf.u8[HEADER_SIZE_B+8],pname);
	sflash_info.databuf.u8[HEADER_SIZE_B+34]=flags;
	res=sflash_SectorWrite(sfun_adr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B);   
	if(res) return res;

	pdesc->_head_sadr=sfun_adr;
	pdesc->_wrk_sadr=sfun_adr;

	sflash_info.files_active++;
	return 0;
}

/* Write to File */
int16_t fs_write(FS_DESC *pdesc, uint8_t *pdata, uint32_t len){
	int16_t res;
	uint32_t maxwrite;
	uint32_t wlen;
	uint32_t newsect;

	if(!pdesc->_head_sadr) return -117;
	if(pdesc->open_flags & SF_OPEN_RAW){
		if(pdesc->file_pos!=pdesc->file_len) return -130;
	}else if(!(pdesc->open_flags & SF_OPEN_WRITE)) return -118;

	while(len){
		maxwrite=SF_SECTOR_PH-pdesc->_sadr_rel;
		if(maxwrite>SF_SECTOR_PH) return -112;
		if(!maxwrite){
			newsect = sflash_get_free_sector();
			if(!newsect) return -113;

			res=sflash_SectorWrite(pdesc->_wrk_sadr+8,(uint8_t*)&newsect, 4);
			if(res) return res;

			pdesc->_wrk_sadr=newsect;
			pdesc->_sadr_rel=HEADER_SIZE_B;
			maxwrite=SF_SECTOR_PH-HEADER_SIZE_B;
			sflash_info.databuf.u32[0]=SECTOR_MAGIC_DATA;
			sflash_info.databuf.u32[1]=pdesc->_head_sadr;
			res=sflash_SectorWrite(pdesc->_wrk_sadr, (uint8_t*)&sflash_info.databuf, 8);
			if(res) return res;
			sflash_info.available_disk_size-=SF_SECTOR_PH;
		}

		wlen=len;
		if(wlen>maxwrite) wlen=maxwrite;
		if(pdesc->open_flags & SF_OPEN_CRC) pdesc->file_crc32 = fs_track_crc32(pdata, wlen, pdesc->file_crc32);
		res=sflash_SectorWrite(pdesc->_wrk_sadr+pdesc->_sadr_rel, pdata, wlen);
		if(res) return res;
		len-=wlen;
		pdata+=wlen;
		pdesc->_sadr_rel+=wlen;
		pdesc->file_pos+=wlen;
		if(pdesc->file_len!=0xFFFFFFFF) pdesc->file_len=pdesc->file_pos;
	}
	return 0;
}

int16_t fs_close(FS_DESC *pdesc){
	int16_t res;
	uint32_t s0adr;
	uint32_t hinfo[2];

	if(!pdesc->_head_sadr) return -117;
	if(!(pdesc->open_flags & SF_OPEN_WRITE)) return -118;
	s0adr=pdesc->_head_sadr;
	pdesc->_head_sadr=0;
	if(pdesc->open_flags & SF_OPEN_WRITE){
		if(sflash_sadr_invalid(s0adr)) return -120;
		hinfo[0]=pdesc->file_pos;
		hinfo[1]=pdesc->file_crc32;
		res= sflash_SectorWrite(s0adr+HEADER_SIZE_B+0,(uint8_t*)hinfo, 8);
		if(res) return res;
	}
	return 0;   // OK
}

uint32_t fs_get_crc32(FS_DESC *pdesc){
	uint32_t rd_crc;
	if(!pdesc->_head_sadr) return 0;
	sflash_read(pdesc->_head_sadr+HEADER_SIZE_B+4,(uint8_t*)&rd_crc,4);
	return rd_crc;
}

int16_t fs_delete(FS_DESC *pdesc){
	int16_t res;
	if(!pdesc->_head_sadr) return -117;
	if(pdesc->open_flags & SF_OPEN_WRITE) return -125;
	res=flash_set2delete(pdesc->_head_sadr);
	if(res) return res;
	pdesc->_head_sadr=NULL; // No Close!
	return 0;
}

/* Rename a File. Can also be used to change the File's Management Flags (e.g Hidden, Sync), but not CRC...
* But always a new name must be used */
int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc){
    uint16_t mlen;
    uint32_t thdr[5];
    int16_t res;

    if(!pd_odesc->_head_sadr || !pd_ndesc->_head_sadr) return -135;
    if(pd_ndesc->open_flags&(SF_OPEN_READ|SF_OPEN_RAW)) return -133;
    if(pd_ndesc->file_len) return -134;

    if(pd_odesc->file_len==0xFFFFFFFF) mlen=sflash_find_mlen(pd_odesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,SF_SECTOR_PH-HEADER_SIZE_B-FINFO_SIZE_B);
    else if(pd_odesc->file_len>SF_SECTOR_PH-HEADER_SIZE_B+FINFO_SIZE_B) mlen=SF_SECTOR_PH-HEADER_SIZE_B-FINFO_SIZE_B;
    else mlen=pd_odesc->file_len;

    thdr[0]=SECTOR_MAGIC_HEAD_ACTIVE;
    thdr[1]=0xFFFFFFFF;
    sflash_read(pd_odesc->_head_sadr+8,(uint8_t*)&thdr[2],12);

    res=flash_intrasec_copy(pd_odesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,pd_ndesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,mlen); // S D
    if(res) return res;
    res=sflash_SectorErase(pd_odesc->_head_sadr);
    if(res) return res;
    res=flash_intrasec_copy(pd_ndesc->_head_sadr+HEADER_SIZE_B+8,pd_odesc->_head_sadr+HEADER_SIZE_B+8,mlen+FINFO_SIZE_B-8); // S D
    if(res) return res;

    res=sflash_SectorWrite(pd_odesc->_head_sadr,(uint8_t*)thdr, 20);
    if(res) return res;

    pd_ndesc->open_flags=0;
    res=fs_delete(pd_ndesc);
    if(res) return res;

    pd_ndesc->_head_sadr=0;
    return 0;
}

int16_t fs_info(FS_STAT *pstat, uint16_t fno ){
	uint32_t sadr,idx_adr;
	int16_t ret;
	idx_adr=HEADER_SIZE_B+fno*4;
	if(idx_adr>SF_SECTOR_PH-4) return -119;
	sflash_read(idx_adr,(uint8_t*)&sadr,4);
	if(sadr==0xFFFFFFFF) return 0;
	if(sadr>=sflash_info.total_flash_size) return -115;

	sflash_read(sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B);

	switch(sflash_info.databuf.u32[0]){
	case SECTOR_MAGIC_HEAD_ACTIVE:
		ret=FS_STAT_ACTIVE;
		break;
	case SECTOR_MAGIC_HEAD_DELETED:
		ret=FS_STAT_INACTIVE;
		break;
	default: return -126;
	}

	pstat->_head_sadr=sadr;
	fs_strcpy(pstat->fname,(char*)&sflash_info.databuf.u8[HEADER_SIZE_B+8]);
	sadr=sflash_info.databuf.u32[HEADER_SIZE_L];
	if(sadr==0xFFFFFFFF){   // Unclosed File
		ret|=FS_STAT_UNCLOSED;
	}
	pstat->file_len = sadr;
	pstat->file_crc32=sflash_info.databuf.u32[HEADER_SIZE_L+1];
	pstat->disk_flags=sflash_info.databuf.u8[HEADER_SIZE_B+34];
	return ret;
}

//------------------- HighLevel FS OK ------------------------

//----------------------------------------------- JESFS-End ----------------------
