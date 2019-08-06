/*****************************************************************
* blackbox_helpers.h
*
* Source-File with some less important stuff for the blackbox
*******************************************************************************/

#include "blackbox_helpers.h" // Some unimportant stuff for this demo
#include "JesFs.h"

//************** Globals ****************
FS_DATE fs_date; // A JesFs-Structure for dates


//**************** Windows **************
#ifdef __WIN32__

// * Get a String from the UART , simulated on PC as gets()
int uart_gets(uint8_t* buf, int16_t max, uint16_t wt, uint8_t echo){
	gets((char*)buf);
	return strlen((char*)buf);
}

//*************** TI-RTOS ****************
#else

// External Time-Helper function on CC1310 use either BIOS or AON_RTC
int32_t _time_get(void){
    return Seconds_get();   // use BIOS here
}


// * mainThread init - TI-RTOS inits some HW-Register
void mainThread_init(void){

	/* Init what we need */
	GPIO_init(); // LEDs (and JesFs I/Os too)
	/* Configure the LED pin */
	GPIO_setConfig(Board_GPIO_RLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW); // RED
	// GPIO_setConfig(Board_GPIO_GLED, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_HIGH); // GREEN

	UART_init(); // required for HW uart
	SPI_init();  // required for JesFs
}

#endif

// * Show used board/environment
void helper_show_board(void){
/*** Show Board ***/
#ifdef CC1310_LAUNCHXL
	uart_printf("Board: CC1310_LAUNCHXL\n");
#elif defined(__WIN32__)
	uart_printf("Board: WIN32-PC\n");
#elif defined(CC1350_LAUNCHXL)
	#warn "Remark: For CC1350_LaunchXL this define might be missing in 'board.h': (add manually): '#define Board_GPIO_SPI_FLASH_CS CC1350_LAUNCHXL_GPIO_SPI_FLASH_CS'"
	uart_printf("Board: CC1350_LAUNCHXL\n");
#else
 #error "Unknown Board"
#endif
}

#ifdef __WIN32__
#define USE_DEFAULT_DISK   // Enable for persistance
// * Only on Windows - Load Disk from file (if available)
void helper_load_disk_from_file(void){
 #ifdef USE_DEFAULT_DISK
	int16_t res;
	// There may be an old image available
	uart_printf("__WIN32__ Read VirtualDisk from File: 'default.disk' -> ");
	fs_start(FS_START_NORMAL); // On PC:
	res=ll_read_vdisk("default.disk");  // Read raw image 1:1 to RAM
	if(res) uart_printf("Result: %d\n",res);
	else uart_printf("OK\n");
 #endif
}

// * Only on Windows - Save Disk to file
void helper_save_disk_to_file(void){
 #ifdef USE_DEFAULT_DISK
	int16_t res;
	uart_printf("__WIN32__ Write VirtualDisk to File: 'default.disk' -> ");
	res=ll_write_vdisk("default.disk");
	if(res) uart_printf("Result: %d\n",res);
	else uart_printf("OK\n");
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
		int16_t res;
		int16_t i;

		uart_printf("Disk size: %d Bytes\n",   sflash_info.total_flash_size);
		if(sflash_info.creation_date==0xFFFFFFFF){  // Severe Error
			uart_printf("Error: Invalid/Unformated Disk!\n");
			return;
		}
		uart_printf("Disk available: %d Bytes / %d Sectors\n",sflash_info.available_disk_size,sflash_info.available_disk_size/SF_SECTOR_PH);
		helper_conv_secs_to_date(sflash_info.creation_date, dbuffer);
		uart_printf("Disk formated [%s]\n",dbuffer);
		for(i=0;i<sflash_info.files_used+1;i++){ // Mit Testreserve
			res=fs_info(&fs_stat,i);

			if(res<=0) break;
			if(res&FS_STAT_INACTIVE) uart_printf("(- '%s'   (deleted))\n",fs_stat.fname); // Inaktive/Deleted
			else if(res&FS_STAT_ACTIVE) {
				uart_printf("- '%s'   ",fs_stat.fname); // Active
				if(res&FS_STAT_UNCLOSED) {
					fs_open(&fs_desc,fs_stat.fname,SF_OPEN_READ|SF_OPEN_RAW); // Find out len by DummyRead
					fs_read(&fs_desc, NULL , 0xFFFFFFFF);   // Read as much as possible
					uart_printf("(Unclosed: %u Bytes)",fs_desc.file_len);
				}else{
					uart_printf("%u Bytes",fs_stat.file_len);
				}
				// The creation Flags
				if(fs_stat.disk_flags & SF_OPEN_CRC) uart_printf(" CRC32:%x",fs_stat.file_crc32);
				if(fs_stat.disk_flags & SF_OPEN_EXT_SYNC) uart_printf(" ExtSync");
				//if(fs_stat.disk_flags & _SF_OPEN_RES) uart_printf(" Reserved");

				helper_conv_secs_to_date(fs_stat.file_ctime, dbuffer);
				uart_printf(" [%s]\n",dbuffer);
			}
		}
		uart_printf("Disk Nr. of files active: %d\n",sflash_info.files_active);
		uart_printf("Disk Nr. of files used: %d\n",sflash_info.files_used);
#ifdef JSTAT // if defined in JesFs generate more statistic infos
		if(sflash_info.sectors_unknown) uart_printf("WARNING - Disk Error: Nr. of unknown sectors: %d\n",sflash_info.sectors_unknown);
#endif
		if(res) uart_printf("Result: %d\n",res);
		else uart_printf("OK\n");
		return;
}
