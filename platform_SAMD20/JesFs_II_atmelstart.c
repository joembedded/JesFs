/*******************************************************************************
* JesFs_ll_atmelstart.c: LowLevelDriver for a bare metal atmel start project
*
* JesFs - Jo's Embedded Serial File System
*
* Tested on custom SAMD20e18 PCB
*
* (C) 2019 joembedded@gmail.com - www.joembedded.de
* Version: 1.0 / 13.1.2022
*
*******************************************************************************/

// ---------------- required for JesFs ----------------------------
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "jesfs.h"
#include "jesfs_int.h"


#include <atmel_start.h> // Atmel start header files

//------------------- LowLevel SPI START ------------------------
struct io_descriptor *spi_io;

// INIT Hardware SPI
int16_t sflash_spi_init(void){
    spi_m_sync_get_io_descriptor(&spi_0, &spi_io);
    spi_m_sync_enable(&spi_0);
	return 0;
}

void sflash_spi_close(void){
    spi_m_sync_disable(&spi_0);
}

void sflash_wait_usec(uint32_t usec){
    delay_us(usec);
}

void sflash_select(void){
    gpio_set_pin_level(FLASH_CS, false);
}

void sflash_deselect(void){
    gpio_set_pin_level(FLASH_CS, true);
}

void sflash_spi_read(uint8_t *buf, uint16_t len){
    io_read( spi_io, buf, len );
}

void  sflash_spi_write(const uint8_t *buf, uint16_t len){
    io_write( spi_io, buf, len );
}
//------------------- LowLevel SPI Fertig ------------------------
