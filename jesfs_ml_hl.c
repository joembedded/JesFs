/*******************************************************************************
* JesFs_ml_hl.c: JesFs HighLevel(User) and MidLevel(SPI Management) drivers
*
* JesFs - Jo's Embedded Serial File System
* Tested on Win and TI-RTOS CC1310 Launchpad
*
* (C) joembedded@gmail.com 2018
* Please regard: This is Copyrighted Software!
* It may be used for education or non-commercial use, but without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'
*******************************************************************************/

//#include <stdio.h>
//#define my_printf printf

/* Required Std. headers */
#include <stdint.h>
#include <stddef.h>

//----------------------------------------------- JESFS-START ----------------------
#include "jesfs.h"
#include "jesfs_int.h"

SFLASH_INFO sflash_info; // Beschreibt das Flash

// Driver designed for 4k-Flash - JesFs
#if SF_SECTOR_PH != 4096
 #error "Phyiscal Sector Size SPIFlash must be 4K"
#endif
#if FNAMELEN!=25
 #error "FNAMELEN fixed to 25+Zero-Byte by Design"
#endif

//------------------- MediumLevel SPI Start ------------------------
//* Einzelbyte-Kommando senden.
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

/* ID auf Speichergroesse 8und evtl. Spezialfaelle, bisher aber nix) umsetzen und sflash_info auffuellen */
int16_t sflash_interpret_id(uint32_t id){
	uint8_t h;
	sflash_info.total_flash_size=0; // Annahme
	sflash_info.identification=id; // in jedem Fall mal merken

	// Pruefen von Manufacorer und Typ: Macronix
	switch(id>>8){   // Ohne Density
	default:
		return -104;    // Unbekannter Typ (Achtung: Micron hat andere Typen als Macronix, aber identische Fkt...)

	// List of tested/knownAsGood Flash-Manufacturer/IDs (see Header File).  Others may be added later
	case MACRONIX_MANU_TYP: // The 1MB-Version is on the CC1310 Launchpad
	// ...add others...
		break;      // OK!
	}

#ifdef DEBUG_FORCE_MINIDISK_DENSITY
	h=DEBUG_FORCE_MINIDISK_DENSITY; // MiniDisc >= 2 Sectors Minimum
#else
	h=id&255;   // Density
	if(h<MIN_DENSITY || h>MAX_DENSITY) return -103;  // Unbekannte Density! 8*512kB-8*16MB ist OK
#endif
	sflash_info.total_flash_size=1<<(h);     // Technisch ware alles ab 8k-16MB OK fuer 3B-SPI Flash
	return 0;   // OK
}

/* DEEP POWER DOWN
 * Flash in Tiefschlaf versetzen. Kann nur per RELASE oder (manche) von READ IDENTIFIATION geweckt werden */
#define CMD_DEEPPOWERDOWN 0xB9
void sflash_DeepPowerDown(void){
	sflash_bytecmd(CMD_DEEPPOWERDOWN,0);   // NoMore
}

/* RELEASE from DEEP POWER DOWN
 * Aeltere Flash liefern hier ID, neuere weckt es lediglich. Danach nach tPowerUp ID lesen
 * GANZ WICHTIG: Z.B. MX25R80535 benoetigt danach ca. 35usec bis ansprechbar!
 * Unbedingt beruecksichtigen (separat, User) */
#define CMD_RELEASEDPD 0xAB
void sflash_ReleaseFromDeepPowerDown(void){
	sflash_bytecmd(CMD_RELEASEDPD,0);   // NoMore
	// sflash_wait_usec(50);  // selber machen!
}

/* Len Bytes von FlashAdr(sdr) nach sbuf lesen.
 * Fuer Adr>=16MB: 4-Byte-Kommando verwenden (0x13), separat einbauen
 * Hier unterschiedlich pro Flash!
 * Option: HighSpeed Read
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

/* Len Bytes von FlashAdr(sdr) nach sbuf schreiben.
 * Fuer Adr>=16MB: 4-Byte-Kommando verwenden (0x12), separat einbauen
 * Hier unterschiedlich pro Flash!
 * Option: HighSpeed Write
 * Vorher Enable setzen, wird autom. rueckgesetzt auf 0
 * Dauer ca. 0.8-4 msec/pagewrite
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
 * (Write Enable vorher, takes long! Scan Status)
 * M25P40:  4.5 typ / 10 max sec
 * MX25R8035 25 typ / 75 max sec:
 * MT25QL128: 38 typ / 114 max (sec)
 */
#define CMD_BULKERASE 0xC7
void sflash_BulkErase(void){
	sflash_bytecmd(CMD_BULKERASE,0);   // NoMore
}

/* Sector Erase 4k
 * Achtung: 4k nur vorhanden auf manchen Flash
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


/* Daten in einem Sektor schreiben, bis zur max. Sektorgroesse. Es wird versucht Pages zu schreiben
 ACHTUNG: Nach prg, ist Flash noch eine Weile Busy, man koennte das theoretisch erst vor naechstem Schreibvorgang checken,
 Macht die Sache aber kompliziert bei Wechseln. Daher, lieber hier erstmal nicht so 'performant' */
int16_t sflash_SectorWrite(uint32_t sflash_adr, uint8_t* sbuf, uint32_t len){
	uint32_t maxwrite;

	if(sflash_adr>=sflash_info.total_flash_size) return -105; // Flash voll! Illegale Adresse
	maxwrite=SF_SECTOR_PH-(sflash_adr&(SF_SECTOR_PH-1)); // wieviel darf maximal in diesem Sektor geschrieben werden?
	if(len>maxwrite) return -106;   // Sektorgrenze beim Schreiben verletzt

	while(len){
		maxwrite=256-(uint8_t)sflash_adr; // Soviel in Page erlaubt
#ifdef SF_TX_TRANSFER_LIMIT
		if(maxwrite>SF_TX_TRANSFER_LIMIT) maxwrite=SF_TX_TRANSFER_LIMIT;
#endif
		if(len<maxwrite) maxwrite=len;    // Dann halt weniger in diesem Block schreiben

		if(sflash_WaitWriteEnabled()) return -102; // Wait enabled und warten bis OK, Fehler 1:1
		sflash_PageWrite(sflash_adr, sbuf, maxwrite);
		if(sflash_WaitBusy(100)) return -101; // 100 msec warten bis Page OK, ist zwar technisch nicht ideal, aber logisch einfacher
		//mUART_printf("%d/%d(%d) (%x %x %x %x)\n",sadr,sbuf,maxfrei,sbuf[0],sbuf[1],sbuf[2],sbuf[3]);
		sbuf+=maxwrite;
		sflash_adr+=maxwrite;
		len-=maxwrite;
	}
	return 0;   // Alles OK
}

// HighLevel SectorErase (inc. Error Check and
int16_t sflash_SectorErase(uint32_t sadr){
	if(sflash_WaitWriteEnabled()) return -102; // Wait enabled und warten bis OK, Fehler:
	sflash_llSectorErase4k(sadr);
	if(sflash_WaitBusy(400)) return -101; // 400 msec warten bis evtl. alte Page durch, Fehler:
	return 0;
}

//------------------- MediumLevel SPI Fertig ------------------------

//------------------- HighLevel FS Start ------------------------
// Fast and short helpers - my omit unnecessary libs, slightly modified to standard
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

// Test - Sector zeigt in den gueltigen Datenbereich, nicht aber auf Index 0. Einzig FFFFFFFF ist erlaubt als unused
int16_t sflash_sadr_invalid(uint32_t sadr){
	if(sadr==0xFFFFFFFF) return 0;  // OK
	if(!sadr) return -1;    // Zeigt auf Sektor 0! (kann erlaubt sein)
	if(sadr & (SF_SECTOR_PH-1)) return -2; // Zeigt nicht auf Sektor! FATAL
	if(sadr>=sflash_info.total_flash_size) return -3; // Zeigt auf hinter das Ende der Welt FATAL
	return 0; // Voll gueltig
}

// Komplette Kette entfernen
int16_t flash_set2delete(uint32_t sadr){
	int16_t res;
	uint32_t thdr[3];  // Testheader
	uint32_t max_sect;
	uint32_t oadr;
	oadr=sadr;  // owneradresse merken
	max_sect=(sflash_info.total_flash_size/SF_SECTOR_PH); // Notfalls alles Sektoren durchschauen
	while(--max_sect){
		if(sflash_sadr_invalid(sadr)) return -120;
		sflash_read(sadr,(uint8_t*)thdr,12); // 3 Verwaltungswors holen
		if(thdr[0]==SECTOR_MAGIC_HEAD_ACTIVE){
			if(thdr[1]!=0xFFFFFFFF) return -122;
			thdr[0]=SECTOR_MAGIC_HEAD_DELETED;
			sflash_info.files_active--; // Aber used bleibt der Sektor
		}else if(thdr[0]==SECTOR_MAGIC_DATA){
			if(thdr[1]!=oadr) return -122;
			thdr[0]=SECTOR_MAGIC_TODELETE;
			// Nur Magic-Data erhoeht verfuegbare kapaizaet
			sflash_info.available_disk_size+=SF_SECTOR_PH; // Sektor wird frei
		}else return -123;  // Illegaler Sektortyp

		res= sflash_SectorWrite(sadr,(uint8_t*)thdr, 4); // Sektor
		if(res) return res;

		sadr=thdr[2];   // Follower
		if(sadr==0xFFFFFFFF) return 0;  // Kettenende
		// ansonsten halt weiter...
	}
	return -121;
}

// Helper Fct. 2 find maximum endpos for unclosed file, also useful for
// finding EndOfSector
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

//--------------------------- Ab hier User Functions ----------------------------------------

/* Start Filesystem - Fill structurs and check basic parameters */
int16_t fs_start(uint8_t mode){
	int16_t res;
	uint32_t id;
	uint32_t sadr;
	uint32_t idx_adr;
	uint32_t dir_typ;
	uint16_t err;

	res=sflash_spi_init();
	if(res) return res;        // Fehler an User petzen

	// Flash sicher wecken
	sflash_ReleaseFromDeepPowerDown();
	sflash_wait_usec(45);

	// ID lesenund HW-Teil der Flash-Info fuellen
	id=sflash_QuickScanIdentification();

	if(mode&FS_START_RESTART){
		if(sflash_info.total_flash_size && id==sflash_info.identification){
			//my_printf("RESTART\n");
			return 0;   // Nur wecken, stimmt ja schon alles
		}
	}

	res=sflash_interpret_id(id);
	if(res) return res;        // Fehler an User petzen

	// OK, Flash ist bekannt. Lesen was steht am Anfang? die ersten 4 Eintraege
	sflash_read(0,(uint8_t*)&sflash_info.databuf ,HEADER_SIZE_B); //  Header V1.0 - 4 Longs

	//my_printf("START-MODE:  %X\n",mode);
	//my_printf("MAGIC:   %X\n",sflash_info.databuf.u32[0]);
	//my_printf("FlASHID: %X\n",sflash_info.databuf.u32[1]);

	// Header testen
	if(sflash_info.databuf.u32[0]!=HEADER_MAGIC) return -108;
	if(sflash_info.databuf.u32[1]!=sflash_info.identification) return -109;
	// sflash_info.databuf.u32[2] Bulk/ID, User/ID

	// Disk untersuchen, ab hier Error sammeln bis Ende
	// Zuerst die Sektoren
	err=0;
	sflash_info.available_disk_size=sflash_info.total_flash_size-SF_SECTOR_PH; // 1 Sektor fehlt
	sflash_info.sectors_todelete=0;
	sflash_info.sectors_clear=0;
	sflash_info.sectors_unknown=0;
	sflash_info.files_used=0;   // Alle HEADS zaehlen zu USED, egal ob aktiv oder deleted
	sflash_info.files_active=0; // Nur die verwendeten Files im Dir.

	sflash_info.lusect_adr=0;   // Dieser Sektor wurde zumind. verwendet
	// Nun Alle Sektoren scannen ab Index. Dauert bei 1M-Flash 12msec, 16M-Flash: 200msec (12 NHz SPI)
	for(sadr=SF_SECTOR_PH;sadr<sflash_info.total_flash_size;sadr+=SF_SECTOR_PH){
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,(mode&FS_START_FAST)?4:12); //  L1 oder 3 Long lesen
		switch(sflash_info.databuf.u32[0]){ // Header-Usage
		case 0xFFFFFFFF:    // Formatiert und unbenutzt
			sflash_info.sectors_clear++;
			break;
		case SECTOR_MAGIC_TODELETE:
			sflash_info.sectors_todelete++;
			sflash_info.lusect_adr=sadr;   // Ab diesem Sektor suchen..
			break;

		case SECTOR_MAGIC_HEAD_ACTIVE:      // Datei ist in Gebrauch
			sflash_info.files_active++;
			sflash_info.lusect_adr=sadr;   // Ab diesem Sektor suchen
		case SECTOR_MAGIC_HEAD_DELETED:     // Datei nicht verwendet, aber Sektor ist Platzhalter fuer Dateianfaenge (bis Reorg/neu open)
			sflash_info.files_used++;
			sflash_info.lusect_adr=sadr;   // Ab diesem Sektor suchen

		case SECTOR_MAGIC_DATA:
			sflash_info.available_disk_size-=SF_SECTOR_PH; // Sektor belegt
			sflash_info.lusect_adr=sadr;   // oder ab diesem Sektor..
			break;

		default:
			sflash_info.sectors_unknown++; // !!! Was faul
			err++;
		}
		// Im Sorgaeltigen Modus untersuchen ob Pointer OK sind. Details regelt das Rebuild
		if(!(mode&FS_START_FAST)){
			switch(sflash_info.databuf.u32[0]){
			case 0xFFFFFFFF:
				//my_printf("s:%x-> (FFFFFFFF) %x %x\n",sadr,sflash_info.databuf.u32[1],sflash_info.databuf.u32[2]);
				// Leere Sektoren: Auch die beiden naechsten sind leer 8und Rest hoffentlich auch...
				if(sflash_info.databuf.u32[1]!=0xFFFFFFFF || sflash_info.databuf.u32[2]!=0xFFFFFFFF) err++;
				break;
			case SECTOR_MAGIC_HEAD_ACTIVE:      // Datei ist in Gebrauch
			case SECTOR_MAGIC_HEAD_DELETED:     // Datei nicht verwendet, aber Sektor ist Platzhalter fuer Dateianfaenge (bis Reorg/neu open)
				//if(sflash_info.databuf.u32[0]==SECTOR_MAGIC_HEAD_ACTIVE) my_printf("s:%x-> HEAD_ACTIVE %x %x\n",sadr,sflash_info.databuf.u32[1],sflash_info.databuf.u32[2]);
				//else my_printf("s:%x-> HEAD_DELETED %x %x\n",sadr,sflash_info.databuf.u32[1],sflash_info.databuf.u32[2]);

				if(sflash_info.databuf.u32[1]!=0xFFFFFFFF) err++;   // Index-Sektoren haben keinen Owner

				// NEXT Sektor ist entweder FFFFFFFF oder zeigt auf gueltige Sektorgrenze
				if(sflash_sadr_invalid(sflash_info.databuf.u32[2])) err++;
				break;
			case SECTOR_MAGIC_DATA:
			case SECTOR_MAGIC_TODELETE:
				//if(sflash_info.databuf.u32[0]==SECTOR_MAGIC_DATA) my_printf("s:%x-> DATA %x %x\n",sadr,sflash_info.databuf.u32[1],sflash_info.databuf.u32[2]);
				//else my_printf("s:%x-> TODELETE %x %x\n",sadr,sflash_info.databuf.u32[1],sflash_info.databuf.u32[2]);
				// Owner MUSS gueltig sein, daher FF nicht erlaubt
				idx_adr=sflash_info.databuf.u32[1];
				if(idx_adr==0xFFFFFFFF || sflash_sadr_invalid(idx_adr)) err++;

				// NEXT Sektor ist entweder FFFFFFFF oder zeigt auf gueltige Sektorgrenze
				if(sflash_sadr_invalid(sflash_info.databuf.u32[2])) err++;
				break;
			}
		}
	}

	//my_printf("AVAIL: %d Bytes / %d Sekt.\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
	//my_printf("FILES: ACTIVE: %d, USED: %d\n",sflash_info.files_active,sflash_info.files_used);
	//my_printf("C,toC,???: %d,%d,%d\n",sflash_info.sectors_clear,sflash_info.sectors_todelete,sflash_info.sectors_unknown);
	//my_printf("LUSECT: %x\n",sflash_info.lusect_adr); // Last USED sector

	// Nun die (vorhandenen Dateien scannen, ob zumind. im Index gelistet
	// Index pruefen
	sadr=HEADER_SIZE_B;    // Ab Adr. HEADER_SIZE_B liegen die Eintraege bis Sektor-Ende
	id=0;
	while(sadr!= SF_SECTOR_PH){
		sflash_read(sadr,(uint8_t*)&idx_adr,4); // Einzelne Eintraege lesen (OK, ist langsam!)
		//my_printf("i:%x-> %x\n",sadr,idx_adr);
		// Adresse zeigt nicht auf Sektorgrenze oder sonstwohin, ist ein Fehler
		if(idx_adr==0xFFFFFFFF) break; // Ende der Liste
		else{
			if(sflash_sadr_invalid(idx_adr) ) err++;    // idx_adr muss auf gueltigen Sektor zeigen
			else{ // OK, steht da dann auch ein Dateieintrag?
				sflash_read(idx_adr,(uint8_t*)&dir_typ,4); // Einzelne Eintraege lesen (OK, ist langsam!)
				if(dir_typ==SECTOR_MAGIC_HEAD_ACTIVE || dir_typ==SECTOR_MAGIC_HEAD_DELETED) id++;
				else err++;
			}
		}
		sadr+=4;
	}

	//  my_printf("ERR: %d\n",err); // Last USED sector
	if(err || (uint16_t)id!=sflash_info.files_used ) return -107; // Strukturprobleme?
	return 0;   // Alles OK
}

/* Set Flash to Ultra-Low-Power mode. Call fs_start(FS_RESTART) to continue/wake */
void  fs_deepsleep(void){
	sflash_DeepPowerDown();
}

/* Format Filesystem. May require between 30-120 seconds (even more, see Datasheet) for a 512k-16 MB Flash) */
int16_t fs_format(uint32_t f_id){
	uint32_t sbuf[3];   // Header ufs (sep.)
	int16_t res;

	if(sflash_WaitWriteEnabled()) return -102; // Wait enabled und warten bis OK, Fehler 1:1
	sflash_BulkErase();
	if(sflash_WaitBusy(120000)) return -101; // 100 msec warten bis evtl. alte Page durch, Fehler 1:1

	//my_printf("Write Header\n");
	sbuf[0]=HEADER_MAGIC;   // 'JesF'
	sbuf[1]=sflash_info.identification;
	sbuf[2]=f_id;

	res=sflash_SectorWrite(0, (uint8_t*)sbuf, 12);    // Header V1.0 - 3 Longs an Adr. 0 schreiben, Rest is FF
	if(res) return res;

	// Danach neu starten
	return fs_start(FS_START_NORMAL);
}

// Neuen Sektor suchen lassen, ggfs. einen loeschen, wenn freigegeben
uint32_t sflash_get_free_sector(void){
	uint32_t thdr;  // Testheader
	uint32_t max_sect;
	// Some embedded compilers complain about the Division. In fact, it will result in a shift. So it might be ignored
	max_sect=(sflash_info.total_flash_size/SF_SECTOR_PH);
	while(--max_sect){
		sflash_info.lusect_adr+=SF_SECTOR_PH;    // ersten Sektor nach dem belegten probieren
		if(sflash_info.lusect_adr>=sflash_info.total_flash_size) sflash_info.lusect_adr=SF_SECTOR_PH; // Wieder mit 1. Sektor nach Index probieren
		sflash_read(sflash_info.lusect_adr,(uint8_t*)&thdr,4);

		// Sektor darf NUR aus dem Pool der Daten genommen werden, Head-Sektoren sind tabu
		if(thdr==SECTOR_MAGIC_TODELETE || thdr==0xFFFFFFFF){
			if(thdr==SECTOR_MAGIC_TODELETE){ // 4k loeschen
				if(sflash_SectorErase(sflash_info.lusect_adr)) return 0; // NULL! Error!
			}
			return sflash_info.lusect_adr;  // Der ist jetzt frei (wird aber eh gleich wieder belegt, daher nicht ausbuchen
		} // ansonsten halt weiter...
	}
	return 0;   // Sektor 0 ist niemals frei NULL: Error
}


int32_t fs_read(FS_DESC *pdesc, uint8_t *pdest, uint32_t anz){
	uint32_t h;
	uint32_t next_sect;
	int32_t total_rd=0;     // max 2GB
	uint16_t max_sec_rd;    // Wieviel max. im Sektor gelesen werden darf
	uint16_t uc_mlen;

	if(!pdesc->_head_sadr) return -117;

	while(anz){
		// Header des aktuellen Sektors lesen
		sflash_read(pdesc->_wrk_sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B); // Header des Sektors holen
		h=sflash_info.databuf.u32[0];
		if(h==SECTOR_MAGIC_HEAD_ACTIVE){
			if(sflash_info.databuf.u32[1]!=0xFFFFFFFF) return -128; // Header mit Owner gefunden
		}else if(h==SECTOR_MAGIC_DATA){
			if(sflash_info.databuf.u32[1]!=pdesc->_head_sadr) return -122; // Ownder wrong
		}else return -123;  // Falscher Header

		next_sect=sflash_info.databuf.u32[2];   // valid or ffffffff
		if(sflash_sadr_invalid(next_sect)) return -120;

		while(anz){
			max_sec_rd=(SF_SECTOR_PH-pdesc->_sadr_rel); // 1. Grenze: Wieviel maximal im Sektor gelesen werden darf
			if(max_sec_rd>(SF_SECTOR_PH-HEADER_SIZE_B)) return -129; // th. for Headersizes its a little bit smaller, but nay be useful
			//for manipulating Header data. Hence not checked..

			if(pdesc->file_len!=0xFFFFFFFF) { // Fall 1 Laenge ist bekannt
				h=pdesc->file_len-pdesc->file_pos;
				if(anz>h) anz=h;    // will mehr lesen als vorhanden, clippen

			}else if(next_sect==0xFFFFFFFF){   // Fall 2-a Laenge unbekannt un es gibt keinen next
				uc_mlen=sflash_find_mlen(pdesc->_wrk_sadr+pdesc->_sadr_rel,max_sec_rd);
				pdesc->file_len=pdesc->file_pos+uc_mlen;   // Now we know the End
				if(anz>(uint32_t)uc_mlen) anz=uc_mlen;
			}

#ifdef SF_RD_TRANSFER_LIMIT
			if(pdest && max_sec_rd>SF_RD_TRANSFER_LIMIT) max_sec_rd=SF_RD_TRANSFER_LIMIT;
#endif

			if((uint32_t)max_sec_rd>anz) max_sec_rd=anz;
			//my_printf("Read %d from $%x\n",max_sec_rd, pdesc->_wrk_sadr+pdesc->_sadr_rel);
			if(pdest){  // Wirklich lesen
				sflash_read(pdesc->_wrk_sadr+pdesc->_sadr_rel,pdest,max_sec_rd); // GetData2User
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
				break;  // Header des naechsten Sektors lesen, bzw. evtl. Ende
			}
		}
	}
	return total_rd; // max 2GB
}


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
	uint32_t sfun_adr=0;    // Erster unbenutze Eintrag, falls gefunden

	// Zuerst name checken
	pdesc->_head_sadr=0;   // Erstmal ungueltig
	pdesc->file_crc32=0xFFFFFFFF;

	if(!*pname || fs_strlen(pname)>FNAMELEN) return -110;

	// Zuerst mal suchen ob das File schon existiert
	for(i=0;i<sflash_info.files_used;i++){
		sflash_read(HEADER_SIZE_B+i*4,(uint8_t*)&sadr,4); // Adr des Verwaltungssektors holen
		// Kein Check auf Gueltigkeit, hat init ja schon gemacht
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B); // Adr des Verwaltungssektors holen
		if(sflash_info.databuf.u32[0]==SECTOR_MAGIC_HEAD_DELETED){
			sfun_adr=sadr;  // Interessant: Unbenutzer Eintrag gefunden, mal merken
		}else if(sflash_info.databuf.u32[0]!=SECTOR_MAGIC_HEAD_ACTIVE) return -114; // Nur 2 Arten Sektoren zu Finden erlaubt!
		else if(!fs_strcmp(pname,(char*)&sflash_info.databuf.u8[HEADER_SIZE_B+8])){
			break;  // sadr hat jetzt einen Wert
		}
		sadr=0;
	}
	/* Szenarien:
	 * sadr hat einen Wert: File gefunden
	 * sadr ist 0, aber sfun_adr hat einen Wert: Zumind. Platz dafuer gefunden
	 * bedie 0: Datei neu erzeugen
	 */

	pdesc->open_flags=flags;
	pdesc->_sadr_rel=HEADER_SIZE_B+FINFO_SIZE_B;
	pdesc->file_pos=0;
	pdesc->file_len=0;

	if(sadr){
		// Ein File dieses Namens existiert. Bei Write-Zugriffen evtl. Kette loeschen
		pdesc->_head_sadr=sadr;
		pdesc->_wrk_sadr=sadr;
		pdesc->file_len=sflash_info.databuf.u32[HEADER_SIZE_L+0]; // LEN von Disk merken, kann 0-xx sein, oder FFFFFFFF wenn Unlcosed
		if(flags & (SF_OPEN_READ | SF_OPEN_RAW)) return 0;  // Lesemodus: ALLES OK im  auch

		// In anderen Faellen: Alte Kette loeschen, aber Stelle merken
		res=flash_set2delete(sadr);
		if(res) return res;
		sfun_adr=sadr;  // Und mit dieser Adresse weitermachen. Crate nicht notwendigerweise noetig
	}else{
		// Wenn er hierher kommt gibt es ein File diesen Namens noch nicht
		if(!(flags & SF_OPEN_CREATE)) return -124;

	}

	// CREATE: Es wird in JEDEM Fall ein File gefunden, auch wenn es das noch nicht gibt
	// File in jedem Fall auf 0 verkuerzen (Voraussetzung zum Neuschreiben..)

	if(!sfun_adr){ // Es gibt nix verwendbares->Neuen Index erzeugen
		sfun_adr=sflash_get_free_sector();
		if(!sfun_adr) return -113;
		// Hat es noch Platz im Sektor 0?
		if(HEADER_SIZE_B+sflash_info.files_used*4>=(SF_SECTOR_PH-4)) return -111;
		// Sektor noch im Sektor 0 eintragen
		res=sflash_SectorWrite(HEADER_SIZE_B+sflash_info.files_used*4,(uint8_t*)&sfun_adr, 4);    // Header V1.0 - 3 Longs an Adr. 0 schreiben, Rest is FF
		if(res) return res;
		sflash_info.available_disk_size-=SF_SECTOR_PH;
		sflash_info.files_used++;   // Eine mehr!
	}else{
		// Den alten Head-Sektor vor Gebrauch loeschen
		res=sflash_SectorErase(sfun_adr);
		if(res) return res;
		// Aus inactiv wurde aktiv, aber used bleibt
	}

	// Header zusammenbauen, mit Filname
	fs_memset((uint8_t*)&sflash_info.databuf,0xFF,HEADER_SIZE_B+FINFO_SIZE_B);   // Block fuellen mit Defaults
	// Sektorheader
	sflash_info.databuf.u32[0]=SECTOR_MAGIC_HEAD_ACTIVE;    // Magic am Anfang
	// Owner bleibt FFFFF (V1.0)
	// Next ist noch nicht ausgefuellt

	// Dateiheader
	// LEN bleibt erstmal FFFFFFFF
	// CRC32 bleibt auch erstmal FFFFFFFF
	fs_strcpy((char*)&sflash_info.databuf.u8[HEADER_SIZE_B+8],pname);   // Dateiname eintragen
	sflash_info.databuf.u8[HEADER_SIZE_B+34]=flags;
	//  Verwaltungsflag bleibt FF

	// Den neuen Header schreiben
	res=sflash_SectorWrite(sfun_adr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B);   
	if(res) return res;

	// File zurueckgeben
	pdesc->_head_sadr=sfun_adr;
	pdesc->_wrk_sadr=sfun_adr;

	sflash_info.files_active++; // Auch eines mehr
	return 0;
}

/* Schreibzugriff auf File */
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
		maxwrite=SF_SECTOR_PH-pdesc->_sadr_rel;  // Soviel darf ich ich diesem Sektor noch schreiben
		if(maxwrite>SF_SECTOR_PH) return -112;
		if(!maxwrite){  // Gibt kein Platz mehr! Neuen Sektor anfordern
			newsect = sflash_get_free_sector(); // Einen Sektor allocieren
			//my_printf("ALLOCATE: %x, Nr.%d\n",newsect,newsect/SF_SECTOR_PH);
			if(!newsect) return -113;   // Speicher VOLL, kein Sektor gefunden

			// erstmal den neuen Sektor in den aktuellen eintragen (nur 4 Bytes)
			res=sflash_SectorWrite(pdesc->_wrk_sadr+8,(uint8_t*)&newsect, 4);
			if(res) return res;

			// Nun
			pdesc->_wrk_sadr=newsect; // Wir zeigen jetzt auf den neuen Sektor
			pdesc->_sadr_rel=HEADER_SIZE_B;  // 12 Bytes sind schon weg...
			maxwrite=SF_SECTOR_PH-HEADER_SIZE_B;  // Soviel darf ich ich diesem Sektor noch schreiben AGAIN
			// Der Neue Sektor ist kompeltt FF, lediglich am Anfang Owner und Usage setzen
			sflash_info.databuf.u32[0]=SECTOR_MAGIC_DATA;
			sflash_info.databuf.u32[1]=pdesc->_head_sadr; // Besitzer dieses Sektors
			res=sflash_SectorWrite(pdesc->_wrk_sadr, (uint8_t*)&sflash_info.databuf, 8);
			if(res) return res;
			sflash_info.available_disk_size-=SF_SECTOR_PH;  // eine Sektor fehlt fehlt!
			//my_printf("Disk available: %d Bytes / %d Sekt.\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
		}

		wlen=len;
		if(wlen>maxwrite) wlen=maxwrite;    // nur soviel schreiben wie erlaubt
		if(pdesc->open_flags & SF_OPEN_CRC) pdesc->file_crc32 = fs_track_crc32(pdata, wlen, pdesc->file_crc32);
		res=sflash_SectorWrite(pdesc->_wrk_sadr+pdesc->_sadr_rel, pdata, wlen);
		if(res) return res;
		len-=wlen;
		pdata+=wlen;
		pdesc->_sadr_rel+=wlen; // Kann nun exakt SF_SECTOR_PH sein
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
	pdesc->_head_sadr=0; // Nun in jedem Fall fuer weiters gesperrt!
	if(pdesc->open_flags & SF_OPEN_WRITE){
		// An Pos. Header+0 Laenge eintragen
		if(sflash_sadr_invalid(s0adr)) return -120;
		hinfo[0]=pdesc->file_pos;
		hinfo[1]=pdesc->file_crc32;
		res= sflash_SectorWrite(s0adr+HEADER_SIZE_B+0,(uint8_t*)hinfo, 8); // Sektor durch 2 Bytes Laenge fixieren
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
	pdesc->_head_sadr=NULL; // Kein Close!
	return 0;
}

int16_t fs_info(FS_STAT *pstat, uint16_t fno ){
	uint32_t sadr,idx_adr;
	int16_t ret;
	idx_adr=HEADER_SIZE_B+fno*4;
	if(idx_adr>SF_SECTOR_PH-4) return -119;
	sflash_read(idx_adr,(uint8_t*)&sadr,4);
	if(sadr==0xFFFFFFFF) return 0;  // Listenende
	if(sadr>=sflash_info.total_flash_size) return -115;

	sflash_read(sadr,(uint8_t*)&sflash_info.databuf,HEADER_SIZE_B+FINFO_SIZE_B); // Adr des Verwaltungssektors holen

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
	// Die ben. Infos rauskopieren, AUCH bei inaktiven Files
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

//------------------- HighLevel FS Fertig ------------------------

//----------------------------------------------- JESFS-End ----------------------
