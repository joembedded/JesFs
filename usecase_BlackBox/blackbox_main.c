/*******************************************************************************
* BlackBox_main.c
*
* This Demo shows how to use JesFs for a "BlackBox": a History-Logger.
* 'History' means: The logger can see some history data, but will NEVER
* overflow. So you can use a small part of the Flash for history data e.g.
* for post-mortem analysis
*
*
* This software has been designed an tested with:
* - the free "Embarcadero(R) C++ Builder Community Edition" (for PC)
* - the free CCS-Studio for SimpleLink CPUs (from TI). Tested on CC1310/CC1350,
*   but will run on (almlost) any other SimpleLink-CPU
* - the SEGGER Embedded Studio for Arm (Nordic Edition), free for Nordic CPUs
*
* (C)2019 joembedded@gmail.com - www.joembedded.de
* Version: 1.5 / 25.11.2019
*
*******************************************************************************/

#include "blackbox_helpers.h" // Some unimportant stuff for this demo
#include "tb_tools.h"

/****************************************************************
* - Core of BlackBox-Demo
******************************************************************/
#include "JesFs.h"
#include "JesFs_int.h" // JesFs-helpers like fs_get_time()

#ifdef CC13XX_CC26XX    // Define this Macro in ProjectOptions - PredefinedSymbols
 #define main     mainThread  // main_start is a Task
#endif

/*****************************************************************
* Globals
******************************************************************/
static uint8_t sbuffer[256]; // Test buffer (universal use)

#define MAX_INPUT 80
static char input[MAX_INPUT+1];

#define HISTORY     1000    // Max. History for Data in Bytes
static int32_t value=0;     // Sample Value to record

/*****************************************************************
* Platform specific
******************************************************************/
uint32_t _time_get(void) {
    return tb_time_get();
}


/*******************************************************************
* log_blackbox(char* logtext, uint16_t len)
*
* This funktion logs one line to the the history
********************************************************************/
int16_t log_blackbox(char* logtext, uint16_t len){
	FS_DESC fs_desc, fs_desc_sec;    // 2 JesFs file descriptors
	int16_t res;

	res=fs_start(FS_START_RESTART);
	if(res) return res;

	// Flags (see docu): Create File if not exists and open in RAW-mode,
	// in RAW-Mode file is not truncated if existing
	res=fs_open(&fs_desc,"Data.pri",SF_OPEN_CREATE|SF_OPEN_RAW);
	if(res) return res;

	// Place (internal) file pointer to the end of the file to allow write
	fs_read(&fs_desc,NULL,0xFFFFFFFF); // (dummy) read as much as possible

	// write the new data (ASCII) to the file
	res=fs_write(&fs_desc,(uint8_t*)logtext,len);
	if(res) return res;

	// Show what was written
	tb_printf("Pos:%u Log:%s",fs_desc.file_len,logtext);

	// Now make a file shift if more data than HISTORY
	if(fs_desc.file_len>= HISTORY){

		tb_printf("Shift 'Data.pri' -> 'Data.sec'\n");

		// Optionally delete and (create in any case) backup file
		res=fs_open(&fs_desc_sec,"Data.sec",SF_OPEN_CREATE);
		if(res) return res;

		// rename (full) data file to secondary file
		res=fs_rename(&fs_desc,&fs_desc_sec);
		if(res) return res;
	}
	fs_deepsleep(); // Set Filesystem to UltraLowPowerMode
	return 0;   // OK
}

/*******************************************************************
* run_blackbox(asec)
* Take a record each asec secs, until Data.pri is >= HISTORY,
* then shift it to Data.sec and delete Data.pri.
* This demo uses "unclosed Files", which is very useful here.
* Run recoder loop *FOREVER* or until user hits key
********************************************************************/
int16_t run_blackbox(uint32_t delay_secs){
	uint32_t asecs;
	uint16_t len;
	int16_t res;
    uint32_t i;
	for(;;){    // Forever
		// modify a random value and get time (UNIX seconds)
		value+=(rand()&255)-128;    // Move sample value
		asecs=fs_get_secs();

		// Build the data we want to save: Time + Value
		len=sprintf((char*)sbuffer,"%u %d\n",asecs,value);

		// Filesystem may be sleeping (= UltraLowPowerMode), WAKE fast
		res=log_blackbox((char*)sbuffer, len);
		if(res) break;  // Error?

		// SLEEP
		for(i=0;i<delay_secs;i++){
			tb_delay_ms(1000); // Allow UltraLowPowerMode fpr CPU (UART still on)
			if(tb_kbhit()) break;  // End
		}
		if(tb_kbhit()) break;  // End
	}
	while(tb_kbhit()) tb_getc();    // Clear keys
	tb_printf("Result: %d\n\n",res);    // 0: OK, else see 'jesfs.h'
	fs_start(FS_START_RESTART); // Wake Filesystem, main loop expects it is awake
	return res; // Return last result
}

/***************************************************************
* new_measure()
* Delete all Data Files and start new measure
***************************************************************/
int16_t new_measure(void){
	FS_DESC fs_desc;
	int16_t res;
	res=fs_open(&fs_desc,"Data.sec",SF_OPEN_CREATE);
	if(res) return res;
	res=fs_delete(&fs_desc);
	if(res) return res;
	res=fs_open(&fs_desc,"Data.pri",SF_OPEN_CREATE);
	if(res) return res;
	res=fs_delete(&fs_desc);
	return res;
}

/***************************************************************
* show_file()
* Show all content of a file using sbuffer.
***************************************************************/
int32_t show_file(char* fname){
	FS_DESC fs_desc;
	int32_t res;    // fs_read returns int32
	uint32_t total=0;
	res=fs_open(&fs_desc,fname,SF_OPEN_READ);
	if(res) return res;
	for(;;){
		// tb_printf does not like lines >80 chars, so read less
		res=fs_read(&fs_desc,sbuffer,40); // read max. 40
		if(res<=0) break;
		sbuffer[res]=0; // make it a string
		total+=res;
		tb_printf("%s",sbuffer);
	}
	tb_printf("Total read: %u Bytes\n",total);
	return res; // (fs_close not required in JesFs)
}

/******************************************************************
* M A I N
*******************************************************************/
void *main(void *arg0){
	int32_t res,par;
	char *pc;   // Points to argument
	uint32_t asecs;

	tb_init();

	tb_printf("\n*** JesFs *BlackBox-Test* V1.5 "__TIME__ " " __DATE__ " ***\n\n");
	tb_printf("(C)2019 joembedded@gmail.com - www.joembedded.de\n\n");

#ifdef __WIN32__
	helper_load_disk_from_file();   // Use a File to store JesFs-Disk
#endif

	tb_printf("'f':          Format Disk\n");
	tb_printf("'n':          New Measure, Delete Data Files\n");
	tb_printf("'r NUMBER'    Run BlackBox with Delay NUMBER secs...\n");
	tb_printf("'1'           Show File 'Data.pri'\n");
	tb_printf("'2'           Show File 'Data.sec'\n");
	tb_printf("'v'           Disk Directory\n");
	tb_printf("'! [SECONDS]' Print Time or optionally set UNIX-Seconds\n");
	tb_printf("'q'           Exit (on __WIN32__: save Disk as File 'default.disk')\n\n");

	tb_printf("Init-JesFS: Res:%d\n",fs_start(FS_START_NORMAL));  // Unformated: Return -108 (see jesfs.h)

	while (1) {
		tb_board_led_invert(0); // Toggle LED (if available)
		tb_printf("> ");
		res=tb_gets(input,MAX_INPUT, 60000, 1);   // 60 seconds time with echo
		tb_putc('\n');

		if(res>0){
			pc=&input[1];
			while(*pc==' ') pc++;   // Remove Whitspaces from Input
			switch(input[0]){

			case 'f': // Format Disk
				par=2; // Full:1 Soft:2 (here: Soft is faster, up to 240 secs on Serial Flash)
				tb_printf("'F' Format Serial Flash (Mode:%d) (may take up to 240 secs!)...\n",par);
				tb_printf("Result: %d\n",fs_format(par));
				break;

			case 'n':
				tb_printf("'n' New Measure. Delete Data Files\n");
				tb_printf("Result: %d\n",new_measure()); // 0:OK
				break;

			case 'r': // (0 possible too for FAST data ;-) )
				asecs=strtoul(pc,NULL,0);
				tb_printf("'r' Run BlackBox with Delay %d secs...\n",asecs);
                tb_printf("<Hit any Key to Stop>\n");
				run_blackbox(asecs);
				break;

			case '1':
				tb_printf("'1' Show File 'Data.pri'\n");
				tb_printf("Result: %d\n",show_file("Data.pri")); // 0:OK
				break;
			case '2':
				tb_printf("'2' Show File 'Data.sec'\n");
				tb_printf("Result: %d\n",show_file("Data.sec")); // 0:OK
				break;


			case 'v': // Listing on virtual Disk - Only basic infos. No checks, Version with **File Health-Check** for all files with CRC in JesFs_cmd.c
				tb_printf("'v' Directory:\n");
				helper_show_directory();
				break;

			case 'q':    // Quit: System Reset.
				tb_printf("'q' Exit in 1 sec...\n");
				tb_delay_ms(1000);
				tb_printf("Bye!\n");
#ifdef __WIN32__
				helper_save_disk_to_file();
#endif
				tb_system_reset(); // PC: exit(), TI-RTOS: Up to Debugger what happens: Reset, Stall, Nirvana, .. See TI docu.

			case '!':   // Time Management - Set the embedded Timer (in unix-Seconds)
				asecs=strtoul(pc,NULL,0);
				if(asecs) tb_time_set(asecs); // Only accepted on CPU
				asecs=tb_time_get();
				helper_conv_secs_to_date(asecs, sbuffer);
				tb_printf("'!': Time: [%s] (%u secs)\n",sbuffer,asecs);
				break;

			default:
				tb_printf("???\n");
		   }
		}
	}
}

// END
