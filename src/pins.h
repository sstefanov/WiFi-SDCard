#ifndef _SD_H_
#define _SD_H_

#define SD_CS		4
#define MISO_PIN		12
#define MOSI_PIN		13
#define SCLK_PIN		14
#define CS_SENSE	5

// Use slower SPI speed for better SD card compatibility
// SPI_FULL_SPEED = 50MHz (fast but can cause write errors on some cards)
// SPI_HALF_SPEED = F_CPU/4 (more reliable, recommended)
// SPI_QUARTER_SPEED = F_CPU/8 (slowest, most compatible)
#define SD_SPI_SPEED SPI_HALF_SPEED

#endif