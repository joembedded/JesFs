/*****************************************************************
* blackbox_helpers.h
*
* Include-File with some less important stuff for the blackbox
*******************************************************************************/


#include <stdint.h>
#include <stddef.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef __WIN32__
 // external Low-Level-Helper functions for WIN32 to save RAM as JesFs-File in JesFs_ll_pc.c
 extern int16_t ll_write_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern int16_t ll_read_vdisk(char *fname);   // PC LowLevel LL-Driver
 extern void ll_setid_vdisk(uint32_t id); // PC LowLevel LL-Driver

 void helper_save_disk_to_file(void);
 void helper_load_disk_from_file(void);

#endif

// Common Helper Functions
void helper_show_board(void);
void helper_conv_secs_to_date(uint32_t asecs, uint8_t *sbuffer);
void helper_show_directory(void);

// END

