/*******************************************************************************
* JesFs_ll_pc.c: Simulation of a LowLevel-Driver with RAD Studio C++
*
* JesFs - Jo's Embedded Serial File System
* Tested on Win and TI-RTOS CC1310 Launchpad
*
* This file is a very simple simulation for a serial flash, adapted
* especially to the overlaying driver structure.
*
* (C) 2019 joembedded@gmail.com
* Please regard: This is Copyrighted Software!
* It may be used for education or non-commercial use, but without any warranty!
* For commercial use, please read the included docu and the file 'license.txt'
*******************************************************************************/

// ---------------- required for JesFs ----------------------------
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "jesfs.h"
#include "jesfs_int.h"

// ---------- local headers -------------------------------------
#include <stdio.h> // Fuer printf
#include <stdlib.h>
//#include <assert.h> // Own is better
#define assert(p)   ((p) ? (void)0 : _my_assert(#p, __FILE__, __LINE__))

//#define TRACK_SPI   // If def write logfile spi_track.txt


#ifndef __CONSOLE__
#include <windows.h>
void _my_assert(char *pc, char* pf, int ln){
	wchar_t buf[256]; // wchar_t: size 2 char: size 1
	// incredible size paranoia with strings sice C99
	swprintf(buf,L"'%hs' (File '%hs' Line %d)\n\n*** Exit! ***",pc,pf,ln);
	MessageBox(NULL,buf,L"<ASSERTION FAILED>",MB_OK|MB_ICONERROR);
	exit(-1);
}
#else
void _my_assert(char *pc, char *pf, int ln){
	char key[10];
	printf("<ASSERTION FAILED: '%s' Line %d (Exit: 'q'<NL>)> ",pc, ln);
	gets(key);  // Wait for NL
	if(*key=='q') exit(-1);     // Fast Exit
	printf("<CONT>\n");
}
#endif

//------------------- LowLevel SPI START ------------------------
/****************************************************************
* Select an ID for the simulated Disk 
* here ID (0xC228) is MACRONIX_MANU_TYP, 
* Density is the last Byte as 2eXX in Bytes (2e19 = 512 kByte)
*****************************************************************/
#define SIM_DISK_ID    ((MACRONIX_MANU_TYP<<8)+0x13) // 512kB
//#define SIM_DISK_ID  ((MACRONIX_MANU_TYP<<8)+0x0F) // 32kB


// --- Required and simulated SPI-Commands ---
#define CMD_DEEPPOWERDOWN 0xB9
#define CMD_RELEASEDPD 0xAB
#define CMD_RDID    0x9F    // Read Identification: Manuf.8 Type.8 Density.8
#define CMD_WRITEENABLE 0x06
#define CMD_STATUSREG 0x05
#define CMD_READDATA 0x03
#define CMD_BULKERASE 0xC7
#define CMD_PAGEWRITE 0x02
#define CMD_SECTOR4K_ERASE 0x20

typedef struct{
	uint32_t sim_disk_id_set;  // Wenn 0; Vorgabe. Sonst: SIM_DISK_DENSITY verwenden
	uint32_t sim_disk_id_used;
	uint8_t *pmem;
	uint32_t memsize;
	uint32_t adr_ptr;
	uint32_t state; // State-Machine: >=128: RD follows
	uint8_t select;    // Monitors CS
	uint8_t powerdown;  // -NotUsedInSimulation-
	uint8_t status_reg; // Status-Reg    Bit1:WenableLatchBit Bit0:WriteInPrograssBit
	FILE *trf;      // trackerFile
} SIM_FLASH;

SIM_FLASH sim_flash;

// INIT Hardware SPI
int16_t sflash_spi_init(void){
	uint32_t i;
	// Remember the ID 
	if(!sim_flash.sim_disk_id_set) sim_flash.sim_disk_id_set=SIM_DISK_ID;

	// If called again: generate new Disk
	if(sim_flash.sim_disk_id_set != sim_flash.sim_disk_id_used){
		if(sim_flash.pmem){
			free(sim_flash.pmem);  // Freigeben
			sim_flash.pmem=NULL;
		}
	}

	sim_flash.sim_disk_id_used=sim_flash.sim_disk_id_set;
	sim_flash.memsize=(1<<(sim_flash.sim_disk_id_used&255));
	assert(sim_flash.memsize>=8192 && sim_flash.memsize<=16777216);
	if(sim_flash.pmem==NULL){
		sim_flash.pmem=malloc(sim_flash.memsize);
		assert(sim_flash.pmem);

		// Fill Disk with mit 'trash'
		for(i=0;i<sim_flash.memsize;i++) sim_flash.pmem[i]=(i+0x55)&255;
	}
	sim_flash.state=0;
	sim_flash.select=0; // Not Selected
	sim_flash.powerdown=0;  // Powered!
	sim_flash.status_reg=0; // 
	sim_flash.adr_ptr=0xFFFFFFFF;

#ifdef TRACK_SPI
	// Write a Logfile
	if(!sim_flash.trf){
		sim_flash.trf=fopen("spi_track.txt","w");
	}
#endif
	return 0;
}

void sflash_spi_close(void){ /*NIX*/ }

// Connected a Piezo to GPIO_LED0 ;-) on CC1310
void sflash_wait_usec(uint32_t usec){ /*NIX*/ }

void sflash_select(void){
	sim_flash.select=1;
	sim_flash.state=0;  // Starts with a command!
}

void sflash_deselect(void){
	sim_flash.select=0;
}

void sflash_spi_read(uint8_t *buf, uint16_t len){
	uint32_t adr;
	uint32_t i;

	assert(sim_flash.pmem);
	assert(sim_flash.select==1);
	assert(sim_flash.state>=128);
	switch(sim_flash.state){
	case 128:               // Read ManuTypDen
		assert(len==3);
		// Nimm Macronix zum simulieren
		buf[0]=(uint8_t)(MACRONIX_MANU_TYP>>8);    // Manu
		buf[1]=(uint8_t)(MACRONIX_MANU_TYP);      // Typ
		buf[2]=(uint8_t)(sim_flash.sim_disk_id_used); // Density
		sim_flash.state=0;
		break;
	case 129:   // Read Status - As often as you want
		assert(len==1);
		buf[0]=sim_flash.status_reg;
		sim_flash.status_reg=0;  // No Wait..
		break;
	case 130:  // ReadData
		adr=sim_flash.adr_ptr;
		for(i=0;i<(uint32_t)len;i++){
			assert(adr<sim_flash.memsize);
			buf[i]=sim_flash.pmem[adr++];
		}
		if(sim_flash.trf) fprintf(sim_flash.trf,"read %x[%x]\n",adr,len);
		break;

	default:
		printf("<LL: ERROR State:%d - Read len:%d Bytes>");
		assert(0);
	}
}


/* Because of SPI page-Size (def. 256 Bytes) len is no problem. */
void sflash_spi_write(const uint8_t *buf, uint16_t len){
	uint32_t adr;
	uint32_t mx;
	uint32_t i;
	uint8_t o,w;

	/*
	printf("<LL: OK: Write:"); // Optionally show packets
	for(i=0;i<len;i++) printf(" %02X",buf[i]);
	printf(">\n");
	*/

	assert(sim_flash.select==1);
	assert(sim_flash.state<128);
	if(!sim_flash.state){

		switch(*buf){
		case CMD_DEEPPOWERDOWN:
			assert(len==1);
			sim_flash.powerdown=1;
			break;
		case CMD_RELEASEDPD:
			assert(len==1);
			sim_flash.powerdown=0;
			break;
		case CMD_RDID:
			assert(len==1);
			sim_flash.state=128; // -> ID follows
			break;
		case CMD_WRITEENABLE:
			assert(len==1);
			sim_flash.status_reg|=2;
			break;
		case CMD_STATUSREG:
			assert(len==1);
			sim_flash.state=129; // -> Status Rd
			break;

		case CMD_READDATA:
			assert(len==4);
			adr=(buf[1]<<16)+(buf[2]<<8)+buf[3];
			sim_flash.adr_ptr=adr;
			sim_flash.state=130; // -> Lesen
			break;

		case CMD_PAGEWRITE:
			assert(len==4); // MY Pagewrite: 2 Transfers : ADR DATA
			adr=(buf[1]<<16)+(buf[2]<<8)+buf[3];
			sim_flash.adr_ptr=adr;
			sim_flash.state=1; // -> Write allowed
			break;

		case CMD_SECTOR4K_ERASE:
			assert(len==4); // MY Pagewrite: 2 Transfers : ADR DATA
			adr=(buf[1]<<16)+(buf[2]<<8)+buf[3];
			assert(adr<sim_flash.memsize);
			assert((adr&(SF_SECTOR_PH-1))==0);
			for(i=0;i<SF_SECTOR_PH;i++){
				sim_flash.pmem[adr+i]=0xFF;
			}
			if(sim_flash.trf) fprintf(sim_flash.trf,"er4k %x\n",adr);
			break;

		case CMD_BULKERASE:
			assert(len==1);
			// Fill Disk with mit 'FF'
			for(i=0;i<sim_flash.memsize;i++) sim_flash.pmem[i]=0xFF;
			if(sim_flash.trf) fprintf(sim_flash.trf,"erase_bulk\n");
			break;

		default:
			printf("<LL: ERROR: Write CMD:%02X",*buf);
			for(i=1;i<(uint32_t)len;i++) printf(" %02X",buf[i]);
			printf(">\n");
			assert(0);
			return ;
		}
	}else{
		switch(sim_flash.state){
		case 1: // 2.nd Block of Write Cmd
			adr=sim_flash.adr_ptr;
			mx=256-(adr&255);   // Maximum allowed pagewrite
			assert(len<=(uint16_t)mx);
			for(i=0;i<(uint32_t)len;i++){
				assert(adr<sim_flash.memsize);
				o=sim_flash.pmem[adr];  // Original
				w=buf[i];   // Write (can only write 0)
				sim_flash.pmem[adr]=(o&w);  // Can only write Zeros..
				adr++;
			}
			if(sim_flash.trf) fprintf(sim_flash.trf,"pagew %x[%x]\n",adr,len);

			break;

		default:
			printf("<LL: ERROR: Write:");
			for(i=0;i<(uint32_t)len;i++) printf(" %02X",buf[i]);
			printf(">\n");
			assert(0);
			return ;
		}
	}
}

//------------------- LowLevel SPI Fertig ------------------------

//------------------- Persistence-Funktion for the Disk ---------
/* Returns:
* 0: OK
* -200: Filename too short
* -201: OpenFileError Read
* -202: Write Error
* -203: OpenFileError Write
* -204: Read Error
* -205: File larger than expected
* -206: No Disk
*/

// Read virtual Disk form File
int16_t ll_write_vdisk(char *fname){
	FILE *outf;
	int wlen;

	if(strlen(fname)<1) return -200;
	if(!sim_flash.pmem) return -206;    // No Disk
	outf=fopen(fname,"wb");
	if(!outf) return -201;
	wlen=fwrite(sim_flash.pmem,1,sim_flash.memsize,outf);
	fclose(outf);
	if(wlen!=(int)sim_flash.memsize) return -202;
	return 0;
}

// Write Virtual Disk to a File. No Disk-ID added. Byte 0 in File is 0 in Flash
int16_t ll_read_vdisk(char *fname){
	FILE *inf;
	int rlen,rlen2,dummy;
	if(strlen(fname)<1) return -200;
	if(!sim_flash.pmem) return -206;    // No Disk
	inf=fopen(fname,"rb");
	if(!inf) return -203;
	rlen=fread(sim_flash.pmem,1,sim_flash.memsize,inf);
	rlen2=fread(&dummy,1,1,inf);
	fclose(inf);
	if(rlen2>0) return -205;
	if(rlen!=(int)sim_flash.memsize) return -204;
	return 0;
}

// Set ID for virtial Disk
int16_t ll_setid_vdisk(uint32_t id){
	assert((id&255)>=0xD && (id&255)<=0x18); // 8kb-16MB OK
	if(id<256) id|=(MACRONIX_MANU_TYP<<8);
	sim_flash.sim_disk_id_set=id;
	return 0;
}

// Get Info for virtual Disk
int16_t ll_get_info_vdisk(uint32_t *pid_used, uint8_t **pmem, uint32_t *psize){
	if(!sim_flash.pmem) return -206;    // No Disk
	if(pid_used) *pid_used=sim_flash.sim_disk_id_used;
	if(pmem) *pmem=sim_flash.pmem;
	if(psize) *psize=sim_flash.memsize;
	return 0;
}

//------------------- Fertig ------------------------

