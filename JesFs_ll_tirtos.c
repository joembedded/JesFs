/*******************************************************************************
* JesFs_ll_tirtos.c: LowLevelDriver for TI-RTOS
*
* JesFs - Jo's Embedded Serial File System
* Tested on Win and TI-RTOS CC1310 Launchpad
*
* Developed on CC1310 Launchpad. 
* Speical notes for TI-RTOS:
* - There is a blocking waiting function used (CPUdelay()). It my be replaced by
*   a TI-RTOS waiting function (like Task_wait(), etc..
* - During SPI select active, the RED Led is on. While waiting it is toggeling,
*   so you can 'hear' and 'see' the file system working ;-)
*
* (C) joembedded@gmail.com 2018
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


/* Driver Header files */
#include <ti/drivers/SPI.h>
#include <ti/drivers/GPIO.h>
/* Board Header file */
#include "Board.h"



//------------------- LowLevel SPI START ------------------------
SPI_Handle flash_spi_handle; // NULL: Initialisieren, sonst: schon aktiv

// INIT Hardware SPI
int16_t sflash_spi_init(void){
	SPI_Params spiParams;

	if(flash_spi_handle) return 0;  // Schon aktiv!

    /* Configure the Flash CS pin (High) Achtung: Namenskonvention _GPIO_ via GPIO*/
    GPIO_setConfig(CC1310_LAUNCHXL_GPIO_SPI_FLASH_CS, GPIO_CFG_OUT_OD_PU | GPIO_CFG_OUT_STR_MED | GPIO_CFG_OUT_HIGH);

	SPI_Params_init(&spiParams);
	//spiParams.frameFormat=SPI_POL0_PHA0 (=Default) oder SPI_POL1_PHA1; beide OK
	//spiParams.bitRate=100000; // Mit 100k zum Testen
	//spiParams.bitRate=1000000; // Mit 1M (=Default) laeufts top
	//spiParams.bitRate=4000000; // Mit 4M laeufts auch
	spiParams.bitRate=12000000; // Mit 12M anscheinend auch
	// lt. TI: Bis 12 MHz mgl. mit TI-RTOS

	spiParams.transferMode = SPI_MODE_BLOCKING;
	spiParams.transferCallbackFxn = NULL;

	flash_spi_handle = SPI_open(Board_SPI0, &spiParams);   // Close fehlt noch...
	if (!flash_spi_handle ) return -100;   // Watchdog will kill
	return 0;
}

void sflash_spi_close(void){
	SPI_close(flash_spi_handle);
	flash_spi_handle=NULL;
}

// Connected a Piezo to GPIO_LED0 ;-)
void sflash_wait_usec(uint32_t usec){
	GPIO_toggle(Board_GPIO_LED0);   // Hear Me Working!
	CPUdelay(usec*12);  // Ausgemessen auf CC1310@48MHz, Releas&Debug
}

void sflash_select(void){
	GPIO_write(Board_GPIO_LED0, 1);   // Hear Me Working! Led ON
	GPIO_write(CC1310_LAUNCHXL_GPIO_SPI_FLASH_CS, 0);
}
void sflash_deselect(void){
	GPIO_write(Board_GPIO_LED0, 0);   // Hear Me Working! Led OFF
	GPIO_write(CC1310_LAUNCHXL_GPIO_SPI_FLASH_CS, 1);
}

/* !!! Attention: TI-RTOS may have problems with the heap, if len is too large
* Seems to work well with DriverLib, but with TI-RTOS drivers only <=1k seems to work well */
void sflash_spi_read(uint8_t *buf, size_t len){
	SPI_Transaction masterTransaction;

	masterTransaction.count = len;
	masterTransaction.txBuf = NULL;
	masterTransaction.arg = NULL;
	masterTransaction.rxBuf = buf;
	SPI_transfer(flash_spi_handle, &masterTransaction);
}

/* Because of SPI page-Size (def. 256 Bytes) len is no problem. */
void  sflash_spi_write(const uint8_t *buf, size_t len){
	SPI_Transaction masterTransaction;

	masterTransaction.count  = len;
	masterTransaction.txBuf  = (void*)buf;
	masterTransaction.arg    = NULL;
	masterTransaction.rxBuf  = NULL;

	SPI_transfer(flash_spi_handle, &masterTransaction);
}
//------------------- LowLevel SPI Fertig ------------------------
