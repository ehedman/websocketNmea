#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include "wsocknmea.h"

/*
 * ADC code for MCP3208 SPI Chip 12 bit ADC
 * This code has been tested on an RPI 3
 */

static struct spi {
    int spiFd;
    unsigned char mode;
    unsigned char bitsPerWord;
    unsigned int speed;
    int status;
} spiDev;

static int spiOpen(char * devspi)
{
     if ((spiDev.spiFd = open(devspi, O_RDWR)) <0) {
          printlog("ADC: Could not open SPI device %s", devspi);
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MODE, &spiDev.mode) < 0) {
          printlog("ADC: Could not set SPIMode (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MODE, &spiDev.mode) <0) {
          printlog("ADC: Could not set SPIMode (RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_BITS_PER_WORD, &spiDev.bitsPerWord) <0) {
          printlog("ADC: Could not set SPI bitsPerWord (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_BITS_PER_WORD, &spiDev.bitsPerWord) < 0) {
          printlog("ADC: Could not set SPI bitsPerWord(RD)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_WR_MAX_SPEED_HZ, &spiDev.speed) < 0) {
          printlog("ADC: Could not set SPI speed (WR)...ioctl fail");
          return 1;
     }

     if (ioctl (spiDev.spiFd, SPI_IOC_RD_MAX_SPEED_HZ, &spiDev.speed) <0) {
          printlog("ADC: Could not set SPI speed (RD)...ioctl fail");
          return 1;
     }
     return 0;
}

// Init ADC
int mcp3208SpiInit(char *devspi, unsigned char spiMode, unsigned int spiSpeed, unsigned char spibitsPerWord)
{
     spiDev.mode = spiMode ;
     spiDev.bitsPerWord = spibitsPerWord;
     spiDev.speed = spiSpeed;
     spiDev.spiFd= -1;

     return spiOpen(devspi);
}

int adcInit(char *device, int a2dChannel)
{
    // check channel for adc
    if (a2dChannel > 7) {
        printlog("ADC: Channel must be less than 8 not  %d", a2dChannel);
        return 1;
    }

    if (!spiDev.status) {
        // Initialize ADC device
        if ( mcp3208SpiInit(device, SPI_MODE_0, 1000000, 8) ) {
            spiDev.status = 0;
            return 1;
        }
    }
    spiDev.status = 1;
    return 0;
}

/********************************************************************
 * This function writes data "data" of length "length" to the spidev
 * device. Data shifted in from the spidev device is saved back into
 * "data".
 * ******************************************************************/
static int spiWriteRead( unsigned char *data, int length){

     struct spi_ioc_transfer spi[length];
     int i;

     // one spi transfer for each byte

     for (i = 0 ; i < length ; i++) {
          memset(&spi[i], 0, sizeof (spi[i]));
          spi[i].tx_buf        = (unsigned long)(data + i); // transmit from "data"
          spi[i].rx_buf        = (unsigned long)(data + i) ; // receive into "data"
          spi[i].len           = sizeof(*(data + i)) ;
          spi[i].delay_usecs   = 0 ;
          spi[i].speed_hz      = spiDev.speed ;
          spi[i].bits_per_word = spiDev.bitsPerWord ;
          spi[i].cs_change = 0;
     }

     return ioctl (spiDev.spiFd, SPI_IOC_MESSAGE(length), &spi);
}

int adcRead(int a2dChannel)
{
    int a2dVal;
    unsigned char data[3];
    
    if (!spiDev.status)
        return 0;

    a2dVal = 0;

    // Do A/D conversion
    data[0] = 0x06 | ((a2dChannel & 0x07) >> 7);
    data[1] = ((a2dChannel & 0x07) << 6);
    data[2] = 0x00;

    (void)spiWriteRead(data, sizeof(data));

    data[1] = 0x0F & data[1];
    a2dVal = (data[1] << 8) | data[2]; 

    return (a2dVal);
}



