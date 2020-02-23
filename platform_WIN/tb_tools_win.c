/*********************************************************************
* tb_tools_win.c - Toolbox for UART, Unix-Time, ..
*
* For Platform __WIN32__ / WIN32 
* - "Embarcadero(R) C++ Builder Community Edition" (for PC)
* - "Microsoft Visual Studio Community 2019"
*
* 2019 (C) joembedded.de
* Version: 
* 1.0  / 25.11.2019
* 1.02 / 09.12.2019 return empty string on timeout
*********************************************************************/

#ifdef WIN32		// Visual Studio Code defines WIN32
 #define _CRT_SECURE_NO_WARNINGS	// VS somtimes complains traditional C
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h> // for var_args
#include <time.h>
#include <conio.h> // kbhit(), getc()
#include <windows.h>
#include <synchapi.h> // Sleep()

#ifdef WIN32		// Visual Studio Code defines WIN32
 #define __WIN32__	
 #define getch		_getch			// VS uses POSIX names
 #define kbhit		_kbhit
#endif

#ifndef __WIN32__	// Embarcadero defines __WIN32__
#error "Toolbox for __WIN32__ / WIN32"
#endif

// ---- local defines --------------

// ---------- locals uart --------------------

// ---- locals Watcdog ----


// --- locals timestamp ----
static bool tb_init_flag=false;

// -------- init Toolbox -----------------
void tb_init(void){
	tb_init_flag=true;

}

// ------ uninit all --------------------
void tb_uninit(void){
	// ToDo...

	tb_init_flag=false;
}

// ------ board support pakage -----
// No LEDs on PC
void tb_board_led_on(uint8_t idx){
}
void tb_board_led_off(uint8_t idx){
}
void tb_board_led_invert(uint8_t idx){
}


// ------- System Reset -------------
void tb_system_reset(void){
	exit(-1);
}


// ------Watchdog, triggered befor reset -----
static void tb_wdt_event_handler(void){
   // ToDo...
  for(;;); // Wait2Die
}

// ---- enable the watchdog only once! -----------
// No Watchdog on PC
void tb_watchdog_init(void){
}

// ---- Feed the watchdog, No function if WD not initialised ------------
// feed_ticks currently ignored, but >0
void tb_watchdog_feed(uint32_t feed_ticks){
}


// --- A low Power delay ---
void tb_delay_ms(uint32_t msec){
	Sleep(msec);
}

// ---- Unix-Timer. Must be called periodically to work, but at least twice per RTC-overflow (512..xx secs) ---
uint32_t tb_time_get(void){
   return (uint32_t)time(NULL);
}

// Set time, regarding the timer
void tb_time_set(uint32_t new_secs){
	printf("__WIN32__: tb_time_set() ignored\n");
}

// ----- fine clocl ticks functions ---------------------
// Use the difference of 2 timestamps to calclualte msec Time
uint32_t tb_get_ticks(void){
  return GetTickCount();  // msec on PC
}

uint32_t tb_deltaticks_to_ms(uint32_t t0, uint32_t t1){
  uint32_t delta_ticks = t1-t0;
	return delta_ticks;
}



// tb_printf(): printf() to toolbox uart. Wait if busy
void tb_printf(char* fmt, ...){
	va_list argptr;
	va_start(argptr, fmt);

	vprintf( fmt, argptr);  // vsn: limit!
	va_end(argptr);
}

// tb_putc(): Wait if not ready
void tb_putc(char c){
  tb_printf("%c",c);
}

// ---- Input functions 0: Nothing available (tb_kbhit is faster than tb_getc) ---------
int16_t tb_kbhit(void){
	int16_t res;
	res=kbhit();
	return res;
}

// ---- get 1 char (0..255) (or -1 if nothing available)
int16_t tb_getc(void){
	int c;
	if(!kbhit()) return -1;
	c=getch();
	return (int16_t)c;
}

// Get String with Timout (if >0) in msec of infinite (Timout 0), No echo (max_uart_in without trailing \0!)
int16_t tb_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo){
	int16_t idx=0;
	int16_t res;
	char c;
	max_uart_in--;  // Reserve 1 Byte for End-0
	for(;;){
		res=tb_kbhit();
		if(res>0){
			res=tb_getc();
            if(res<0) break;
            c=(char)res;
            if(c=='\n' || c=='\r') {
                break;    // NL CR or whatever (no Echo for NL CR)
            }else if(c==8){  // Backspace
               if(idx>0){
                   idx--;
                   if(echo){
                       tb_putc(8);
                       tb_putc(' ');
                       tb_putc(8);
                   }
               }
            }else if(c>=' ' && c<127 && idx<max_uart_in){
                input[idx++]=c;
                if(echo) tb_putc(c);
            }
        }else{
            if(max_wait_ms){
                if(!--max_wait_ms){
					idx=0;  // Return empty String
					break;
				}
            }
            tb_delay_ms(1);
        }
    }
    input[idx]=0;   // Terminate String
    return idx;
}

//**
