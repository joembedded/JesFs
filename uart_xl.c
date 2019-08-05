/***************************************************************************************************************
* uart_xl.c
* Optimised UART driver - quite similar to LTraX's modem_l.x
*
* System works with 2 large background buffers for RX and TX
*
* (C)2018 joembedded
**************************************************************************************************************/

#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Idle.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/hal/Seconds.h>
#include <ti/sysbios/family/arm/m3/Hwi.h>
#include <xdc/cfg/global.h>

/* Example/Board Header files */
#include "Board.h"

#include "uart_xl.h"

#include <stdint.h>
#include <stddef.h>
//#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Driver Header files */
#include <ti/drivers/uart/UARTCC26XX.h>

#include <stdarg.h> // Speziell fuer var_args

//--------------- Globals ------------------------------

// -------------- Locals ------------------------
static UART_Handle uart_handle; // Handle

static uint8_t uart_tx_buf[UART_TX_BUFSIZE];
static Semaphore_Handle uart_tx_sema;

static uint8_t uart_rx_buf[UART_RX_BUFSIZE];
static uint16_t uart_rxidx_in, uart_rxidx_out;
static int16_t uart_rxfrei_anz;


//---------------- Modem blkout with copy ------------------------------------------------------------
// Nach einem Fehler muss Modem geschlossen werden, da sonst uart_tx_sema NIE frei wird!
int16_t uart_blkout(uint8_t* pu, uint16_t len){ 
    if(len>UART_TX_BUFSIZE) {
        return -2002;
    }

    // TX-Puffer 256 Bytes = Nach 22msec muss MUSS alles draussen sein! //  BIOS_WAIT_FOREVER geht anscheinend manchmal schief
    Semaphore_pend(uart_tx_sema, UART_TX_TIMEOUT); // Wenn was schiefgeht: 100 msec (=

    memcpy(uart_tx_buf,pu, len); // D S n
    UART_write(uart_handle,uart_tx_buf,len);
    return 0;
}

//---------------- Single character --------------------------------------------------------------
void uart_putc(char c){
    // TX-Puffer 256 Bytes = Nach 22 muss MUSS alles draussen sein! //  BIOS_WAIT_FOREVER geht anscheinend manchmal schief
    Semaphore_pend(uart_tx_sema, UART_TX_TIMEOUT); // Wenn was schiefgeht: 100 msec (=
    uart_tx_buf[0]=c;
    UART_write(uart_handle,uart_tx_buf,1);
}


// ---- callback Thread ist zu nix zu gebrauchen als Freigabe der Ressourcen ---
static void _uart_handle_write_cb(UART_Handle handle, void *tmp_txBuf, size_t size){

    Semaphore_post(uart_tx_sema);  // Fertig! Scheint manchmal schiefzugehen!
}

// --- read Callback: Es wurden size_bytes gelesen, wieviel weiter
static void _uart_handle_read_cb(UART_Handle handle, void *tmp_txBuf, size_t size){
    uint16_t frei;
    int16_t mxf;
    uint32_t key=Hwi_disable();
    mxf=uart_rxfrei_anz-size;  // Soviel ist JETZT noch frei (int16)

    uart_rxidx_in+=size;
    if(uart_rxidx_in>=UART_RX_BUFSIZE) uart_rxidx_in=0;  // Overflow
    if(uart_rxidx_in>=uart_rxidx_out)  frei=UART_RX_BUFSIZE-uart_rxidx_in;
    else frei=uart_rxidx_out-uart_rxidx_in;
    // Bei FLOW-Ctrl. evtl. kleinere Bloecke als 1/4 lesen fuer feinere Granularitaet
    if(frei>(UART_RX_BUFSIZE/4)) frei=(UART_RX_BUFSIZE/4);

    uart_rxfrei_anz=mxf;   // merken
    Hwi_restore(key);

    UART_read(uart_handle, uart_rx_buf+uart_rxidx_in , frei);  // Empfangen was geht
}

// -------- Anzahl der available Bytes, wenn negativ: Overflow!!! ------------
int16_t uart_kbhit(void){
    int16_t res;
    uint32_t key=Hwi_disable();
    res=UART_RX_BUFSIZE-uart_rxfrei_anz;
    Hwi_restore(key);
    return res;
}

// ---- 1 Zeichen holen oder -1(nix da) oder <-1 Spezialfehler ----------
int16_t uart_getc(void){
    int16_t res;
    int16_t mxf;
    uint32_t key=Hwi_disable();
    if(uart_rxidx_in == uart_rxidx_out){
        mxf=UART_RX_BUFSIZE;  // UART-Puffer ist leer-> Maximaler Fuellstand!
        res=-1;
    }else{
        res=uart_rx_buf[uart_rxidx_out++];    // Clip
        if(uart_rxidx_out==UART_RX_BUFSIZE) uart_rxidx_out=0;
        mxf=uart_rxfrei_anz+1;
    }
    uart_rxfrei_anz=mxf;
    Hwi_restore(key);

    return res;
}

// uart_printf and uart_gets only for tests. Recommended to use the 'terminal.h/-c' functions for projects
/*--------------- buffered printf_implementation -----------------------------------------------
* IMPORTANT: Memory usage during DEBUG can be reduced with "small" printf
* select' NOFLOAT' in Properties->ARM compiler-Advanced Options->Language_Options->Level of printf
* ('MINIMAL' does not support precision and %u) */
void uart_printf(char* fmt, ...){
    size_t ulen;
    va_list argptr;
    va_start(argptr, fmt);

    // TX-Puffer 256 Bytes = Nach 22 muss MUSS alles draussen sein! //  BIOS_WAIT_FOREVER geht anscheinend manchmal schief
    Semaphore_pend(uart_tx_sema, UART_TX_TIMEOUT); // Wenn was schiefgeht: 100 msec (=

    ulen=vsnprintf((char*)uart_tx_buf, UART_TX_BUFSIZE, fmt, argptr);  // vsn: begrenzt!
    va_end(argptr);
    // vsnprintf() limits output to given size, but might return more.
    if(ulen>UART_TX_BUFSIZE-1) ulen=UART_TX_BUFSIZE-1;
    UART_write(uart_handle,uart_tx_buf,ulen);
}

// Get String with Timout (if >0) in msec of infinite, No echo (max_uart_in without trailing \0!)
int16_t uart_gets(char* input, int16_t max_uart_in, uint16_t max_wait_ms, uint8_t echo){
    int16_t idx=0;
    int16_t res;
    char c;
    max_uart_in--;  // Reserve 1 Byte for End-0
    for(;;){
        res=uart_kbhit();
        if(res>0){
            res=uart_getc();
            if(res<0) break;
            c=res;
            if(c=='\n' || c=='\r') {
                break;    // NL CR oder was auch immer (kein Echo fuer NL CR)
            }else if(c==8){  // Backspace
               if(idx>0){
                   idx--;
                   if(echo){
                       uart_putc(8);
                       uart_putc(' ');
                       uart_putc(8);
                   }
               }
            }else if(c>=' ' && c<128 && idx<max_uart_in){
                input[idx++]=c;
                if(echo) uart_putc(c);
            }
        }else{
            if(max_wait_ms){
                if(!--max_wait_ms) break;
            }
            Task_sleep(100);    // Wait 1 msec
        }
    }
    input[idx]=0;   // Terminate String
    return idx;
}



// ----------- uart_open---------------------------
// Manual Flow!
int16_t uart_open(void){
    Semaphore_Params semp;

    // Zuerst die Semaphoren erzeugen!
    // Semaphore fuer TX erzeugen
    Semaphore_Params_init(&semp);
    semp.mode = Semaphore_Mode_BINARY;
    uart_tx_sema=Semaphore_create(1,&semp, NULL); // Geht auch ohne EB

    uart_rxidx_in=0;   // In-Pointer
    uart_rxidx_out=0; // Out-Pointer
    uart_rxfrei_anz=UART_RX_BUFSIZE; // Wieviel verfuegbar insgesamt

    UART_Params uartParams;
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_BINARY; // TEXT nicht auf CC26xx
    uartParams.readDataMode = UART_DATA_BINARY; // TEXT nicht auf CC26xx
    uartParams.readReturnMode = UART_RETURN_FULL;
    uartParams.readEcho = UART_ECHO_OFF; // ECHO ON nicht auf CC26xx
    uartParams.baudRate = 115200;   // <--- 115kBd

    uartParams.readMode = UART_MODE_CALLBACK;
    uartParams.readCallback = _uart_handle_read_cb;
    uartParams.writeMode = UART_MODE_CALLBACK;
    uartParams.writeCallback = _uart_handle_write_cb;

    // Alwasys UART No 0
    uart_handle = UART_open(0, &uartParams); // MUSS klappen!
    if(!uart_handle) return -1004;   // Kann nicht oeffnen!

    UART_control(uart_handle, UARTCC26XX_CMD_RETURN_PARTIAL_ENABLE, NULL); // Nicht noetig bei Einzelzeichen
    UART_read(uart_handle, uart_rx_buf , (UART_RX_BUFSIZE/4));  // Maximal 1/4 Puffer lesen

    return 0;
}

// Kann bedenkenlos jederzeit aufgerufen werden
void uart_close(void){
    if(uart_handle) {
        UART_readCancel(uart_handle);
        // UART_control(uart_handle, UART_CMD_RXDISABLE, 0); // Suggested by TI because of I_q, but no results
        UART_writeCancel(uart_handle);
        UART_close(uart_handle);

        /** Dummy open/close with no callbacks, because of too high I_q */
        UART_Params uartParams;
        UART_Params_init(&uartParams);
        // Alwasys UART No 0
        uart_handle = UART_open(0, &uartParams);
        UART_close(uart_handle);

        uart_handle=NULL;
    }
    Semaphore_delete(&uart_tx_sema);

}

// End
