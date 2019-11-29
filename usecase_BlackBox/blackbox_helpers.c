/*****************************************************************
* blackbox_helpers.c
*
* Source-File with some less important stuff for the blackbox
*******************************************************************************/

#ifdef WIN32		// Visual Studio Code defines WIN32
#define _CRT_SECURE_NO_WARNINGS	// VS somtimes complains traditional C
#endif


#include "blackbox_helpers.h" // Some unimportant stuff for this demo
#include "tb_tools.h"

#include "JesFs.h"

#ifdef WIN32		// Visual Studio Code defines WIN32
	#define __WIN32__	// Embarcadero defines __WIN32__
#endif
#ifdef __WIN32__
	extern int16_t ll_read_vdisk(char* fname);	// Helpers to write/read virtual Disk to fFile on PC
	extern int16_t ll_setid_vdisk(uint32_t id);
	extern int16_t ll_get_info_vdisk(uint32_t* pid_used, uint8_t** pmem, uint32_t* psize);
#endif


//************** Globals ****************
FS_DATE fs_date; // A JesFs-Structure for dates


#ifdef __WIN32__
#define USE_DEFAULT_DISK   // Enable for persistance
// * Only on Windows - Load Disk from file (if available)
void helper_load_disk_from_file(void){
 #ifdef USE_DEFAULT_DISK
	int16_t res;
	// There may be an old image available
	tb_printf("__WIN32__ Read VirtualDisk from File: 'default.disk' -> ");
	fs_start(FS_START_NORMAL); // On PC:
	res=ll_read_vdisk("default.disk");  // Read raw image 1:1 to RAM
	if(res) tb_printf("Result: %d\n",res);
	else tb_printf("OK\n");
 #endif
}

// * Only on Windows - Save Disk to file
void helper_save_disk_to_file(void){
 #ifdef USE_DEFAULT_DISK
	int16_t res;
	tb_printf("__WIN32__ Write VirtualDisk to File: 'default.disk' -> ");
	res=ll_write_vdisk("default.disk");
	if(res) tb_printf("Result: %d\n",res);
	else tb_printf("OK\n");
 #endif
}
#endif

// * Convert a UNIX-Time to date (as string)
void helper_conv_secs_to_date(uint32_t asecs, uint8_t *sbuffer){
	// Use JesFs-builtin Fkt
	fs_sec1970_to_date(asecs,&fs_date);
	sprintf((char*)sbuffer,"%02u.%02u.%04u %02u:%02u:%02u",fs_date.d,fs_date.m,fs_date.a,fs_date.h,fs_date.min,fs_date.sec);
}


// * Show Disk Directory **
void helper_show_directory(void){
		FS_STAT fs_stat;
		FS_DESC fs_desc;
		uint8_t dbuffer[40];   // Date buffer (Text)
		int16_t res=0;
		int16_t i;

		tb_printf("Disk size: %d Bytes\n",   sflash_info.total_flash_size);
		if(sflash_info.creation_date==0xFFFFFFFF){  // Severe Error
			tb_printf("Error: Invalid/Unformated Disk! (-108)\n");
			return;
		}
		tb_printf("Disk available: %d Bytes / %d Sectors\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
		helper_conv_secs_to_date(sflash_info.creation_date, dbuffer);
		tb_printf("Disk formated [%s]\n",dbuffer);
		for(i=0;i<sflash_info.files_used+1;i++){ // Mit Testreserve
			res=fs_info(&fs_stat,i);

			if(res<=0) break;
			if(res&FS_STAT_INACTIVE) tb_printf("(- '%s'   (deleted))\n",fs_stat.fname); // Inaktive/Deleted
			else if(res&FS_STAT_ACTIVE) {
				tb_printf("- '%s'   ",fs_stat.fname); // Active
				if(res&FS_STAT_UNCLOSED) {
					fs_open(&fs_desc,fs_stat.fname,SF_OPEN_READ|SF_OPEN_RAW); // Find out len by DummyRead
					fs_read(&fs_desc, NULL , 0xFFFFFFFF);   // Read as much as possible
					tb_printf("(Unclosed: %u Bytes)",fs_desc.file_len);
				}else{
					tb_printf("%u Bytes",fs_stat.file_len);
				}
				// The creation Flags
				if(fs_stat.disk_flags & SF_OPEN_CRC) tb_printf(" CRC32:%x",fs_stat.file_crc32);
				if(fs_stat.disk_flags & SF_OPEN_EXT_SYNC) tb_printf(" ExtSync");
				//if(fs_stat.disk_flags & _SF_OPEN_RES) tb_printf(" Reserved");

				helper_conv_secs_to_date(fs_stat.file_ctime, dbuffer);
				tb_printf(" [%s]\n",dbuffer);
			}
		}
		tb_printf("Disk Nr. of files active: %d\n",sflash_info.files_active);
		tb_printf("Disk Nr. of files used: %d\n",sflash_info.files_used);
#ifdef JSTAT // if defined in JesFs generate more statistic infos
		if(sflash_info.sectors_unknown) tb_printf("WARNING - Disk Error: Nr. of unknown sectors: %d\n",sflash_info.sectors_unknown);
#endif
		if(res) tb_printf("Result: %d\n",res);
		else tb_printf("OK\n");
		return;
}
