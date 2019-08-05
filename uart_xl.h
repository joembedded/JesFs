/***************************************************************************************************************
* uart_xl.h
* -1002 TX-Block too large
*
*************************************************************************************************************/

#define UART_RX_BUFSIZE  512    // Any size OK ca. 4..Xk..
#define UART_TX_BUFSIZE  512    // Da passt was rein!
#define UART_TX_TIMEOUT	 10000	// Semaphoren-Timeout in Task-Ticks (100 msec enough for 256 Bytes(22 msec))

// Wrapped by 'terminal.h/.c':
int16_t uart_blkout(uint8_t* pu, uint16_t len);
void uart_putc(char c);
int16_t uart_kbhit(void);
int16_t uart_getc(void);
void uart_close(void);
int16_t uart_open(void);

// Only for tests. Recommended to use the 'terminal.h/c'-functions for projects
int16_t uart_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo);
void uart_printf(char* fmt, ...);


// End
