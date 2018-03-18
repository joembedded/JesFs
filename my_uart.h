/* my_uart.h
* Simple printf/gets-replacement for CC13xx etc.. 
* TI-RTOS
*
* (C)2018 www.www.joembedded.de
*/

// Globals/Setup
#define ECHO	// If defined: Uart-ECHO
#define MAX_UART_OUT 120	// Maximum Size Output String
//#define UART_TIMEOUT_MSEC 10000	// If defined, gets() returns after Idle Time - Here: 10000 = 10 sec

void my_printf(char* fmt, ...);
void my_putchar(char c);

int16_t my_gets(char* input, int16_t max_uart_in);

int16_t my_uart_open(void); // Requires (Board) UART_init() called before. Def. ist 115kBd
void my_uart_close(void);

//
