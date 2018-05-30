/*******************************************************************************
* JesFs_hl.c: JesFs HighLevel(User) drivers
*
* JesFs - Jo's Embedded Serial File System
*
* Tested on Win and TI-RTOS CC131x Launchpad
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

#ifdef __WIN32__    // We need Unix-Seconds
 #include <time.h>
#else
 #include <ti/sysbios/hal/Seconds.h>
#endif

// Driver designed for 4k-Flash - JesFs
#if SF_SECTOR_PH != 4096
 #error "Phyiscal Sector Size SPIFlash must be 4K"
#endif
#if FNAMELEN!=21
 #error "FNAMELEN fixed to 21+Zero-Byte by Design"
#endif


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

uint32_t fs_get_secs(void){ // Unix-Secs
#ifdef __WIN32__
	return time(NULL);
#else
	return Seconds_get();
#endif
}

//------ Date-Routines (carefully tested!)---------------
#define SEC_DAY	86400L	// Length of day in seconds for lap year
static const uint32_t daylen[12]={
	31*SEC_DAY,	/* Jan */
	59*SEC_DAY,	/* Feb */
	90*SEC_DAY,	/* Mar */
	120*SEC_DAY, /* Apr */
	151*SEC_DAY, /* Mai */
	181*SEC_DAY, /* June */
	212*SEC_DAY, /* Juli */
	243*SEC_DAY, /* Aug. */
	273*SEC_DAY, /* Sept */
	304*SEC_DAY, /* Oct */
	334*SEC_DAY, /* Nov. */
	365*SEC_DAY, /* Dec. */
};

/* Convert seconds to date-struct */
void fs_sec1970_to_date(uint32_t asecs, FS_DATE *pd){
	uint32_t divs;
	uint8_t dlap=0;
	divs=asecs/(1461*SEC_DAY);
	pd->a=1970+divs*4;
	asecs-=(divs*(1461*SEC_DAY)); // 3 normal + 1 leap year
	if(asecs>=(789*SEC_DAY)){
		asecs-=SEC_DAY;
		dlap=1;
	}
	divs=asecs/(365*SEC_DAY);
	pd->a+=divs;
	asecs-=divs*(365*SEC_DAY);
	if(dlap && asecs<59*SEC_DAY && divs==2) {
		divs=1;
		asecs+=SEC_DAY;
	}else{
		for(divs=0;asecs>=daylen[divs];divs++);
	}
	pd->m=1+divs;
	if(divs) asecs-=daylen[divs-1];
	divs=asecs/SEC_DAY;
	pd->d=1+divs;
	asecs-=SEC_DAY*divs;
	divs=asecs/3600L;
	pd->h=divs;
	asecs-=divs*3600L;
	divs=asecs/60L;
	pd->min=divs;
	asecs-=divs*60L;
	pd->sec=asecs;
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
	sflash_info.creation_date=sflash_info.databuf.u32[2];

	err=0;
	sflash_info.available_disk_size=sflash_info.total_flash_size-SF_SECTOR_PH;

#ifdef JSTAT
	 sflash_info.sectors_todelete=0; // Not really required, just for statistics
	 sflash_info.sectors_clear=0;
	 sflash_info.sectors_unknown=0;
#endif

	sflash_info.files_used=0;
	sflash_info.files_active=0;

	sflash_info.lusect_adr=0;
	// Scan  Takes on 1M-Flash 12msec, 16M-Flash: 200msec (12 MHz SPI)
	for(sadr=SF_SECTOR_PH;sadr<sflash_info.total_flash_size;sadr+=SF_SECTOR_PH){
		sflash_read(sadr,(uint8_t*)&sflash_info.databuf,(mode&FS_START_FAST)?4:12);
		switch(sflash_info.databuf.u32[0]){
		case 0xFFFFFFFF:
#ifdef JSTAT
			sflash_info.sectors_clear++;
#endif
			break;
		case SECTOR_MAGIC_TODELETE:
#ifdef JSTAT
			sflash_info.sectors_todelete++;
#endif
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
#ifdef JSTAT
			sflash_info.sectors_unknown++; // !!! Big Failure
#endif
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
int16_t fs_format(void){
	uint32_t sbuf[3];
	int16_t res;

	if(sflash_WaitWriteEnabled()) return -102; // Wait enabled until OK, Fehler 1:1
	sflash_BulkErase();
	if(sflash_WaitBusy(120000)) return -101; // 100 msec evtl. old page, Fehler 1:1

	sbuf[0]=HEADER_MAGIC;
	sbuf[1]=sflash_info.identification;
	sbuf[2]=fs_get_secs();  // Creation Date der Disk

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
		else if(!fs_strcmp(pname,(char*)&sflash_info.databuf.u8[HEADER_SIZE_B+12])){
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
		if(pdesc->file_len==0xFFFFFFFF) pdesc->open_flags|=SF_XOPEN_UNCLOSED; // informative
		pdesc->open_flags|=(sflash_info.databuf.u8[HEADER_SIZE_B+34]&(SF_OPEN_EXT_SYNC|SF_OPEN_EXT_HIDDEN));
		pdesc->file_ctime=sflash_info.databuf.u32[HEADER_SIZE_L+2]; // get file creation time
		if(flags & (SF_OPEN_READ | SF_OPEN_RAW)) {
			return 0;
		}
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
	fs_strcpy((char*)&sflash_info.databuf.u8[HEADER_SIZE_B+12],pname);
	pdesc->file_ctime=fs_get_secs();
	sflash_info.databuf.u32[HEADER_SIZE_L+2]=pdesc->file_ctime;
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

/* Rename a File. Can also be used to change the File's Management Flags (e.g Hidden, Sync), but not CRC LEN or DATE
* But always a new name must be used */
int16_t fs_rename(FS_DESC *pd_odesc, FS_DESC *pd_ndesc){
	uint16_t mlen;
	uint32_t thdr[6];
	int16_t res;

	if(!pd_odesc->_head_sadr || !pd_ndesc->_head_sadr) return -135;
	if(pd_ndesc->open_flags&(SF_OPEN_READ|SF_OPEN_RAW)) return -133;
	if(pd_ndesc->file_len) return -134;

	if(pd_odesc->file_len==0xFFFFFFFF) mlen=sflash_find_mlen(pd_odesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,SF_SECTOR_PH-HEADER_SIZE_B-FINFO_SIZE_B);
	else if(pd_odesc->file_len>SF_SECTOR_PH-HEADER_SIZE_B-FINFO_SIZE_B) mlen=SF_SECTOR_PH-HEADER_SIZE_B-FINFO_SIZE_B;
	else mlen=pd_odesc->file_len;

	thdr[0]=SECTOR_MAGIC_HEAD_ACTIVE;
	thdr[1]=0xFFFFFFFF;
	sflash_read(pd_odesc->_head_sadr+8,(uint8_t*)&thdr[2],16); // Nx Ln CRC Dt

	res=flash_intrasec_copy(pd_odesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,pd_ndesc->_head_sadr+HEADER_SIZE_B+FINFO_SIZE_B,mlen); // S D
	if(res) return res;
	res=sflash_SectorErase(pd_odesc->_head_sadr);
	if(res) return res;
	res=flash_intrasec_copy(pd_ndesc->_head_sadr+HEADER_SIZE_B+12,pd_odesc->_head_sadr+HEADER_SIZE_B+12,mlen+FINFO_SIZE_B-12); // S D
	if(res) return res;

	res=sflash_SectorWrite(pd_odesc->_head_sadr,(uint8_t*)thdr, 24);
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
	fs_strcpy(pstat->fname,(char*)&sflash_info.databuf.u8[HEADER_SIZE_B+12]);
	pstat->file_crc32=sflash_info.databuf.u32[HEADER_SIZE_L+1];
	pstat->file_ctime=sflash_info.databuf.u32[HEADER_SIZE_L+2];
	pstat->disk_flags=sflash_info.databuf.u8[HEADER_SIZE_B+34];
	sadr=sflash_info.databuf.u32[HEADER_SIZE_L];
	if(sadr==0xFFFFFFFF){   // Unclosed File
		ret|=FS_STAT_UNCLOSED;
		pstat->disk_flags|=SF_XOPEN_UNCLOSED; // informative
	}
	pstat->file_len = sadr;
	return ret;
}

//------------------- HighLevel FS OK ------------------------

//----------------------------------------------- JESFS-End ----------------------
