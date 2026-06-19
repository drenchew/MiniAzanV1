// TFT_eSPI setup — ILI9341 on dedicated UI SPI bus. Included via build_flags.
#include "BoardConfigPins.h"

#define USER_SETUP_LOADED 1

#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_CS   MINI_AZAN_TFT_CS
#define TFT_DC   MINI_AZAN_TFT_DC
#define TFT_RST  MINI_AZAN_TFT_RST
#define TOUCH_CS MINI_AZAN_TOUCH_CS
#define TFT_MOSI MINI_AZAN_TFT_MOSI
#define TFT_SCLK MINI_AZAN_TFT_SCLK
#define TFT_MISO MINI_AZAN_TFT_MISO

#define USE_HSPI_PORT
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

#define LOAD_GLCD
#define LOAD_FONT2
#define SMOOTH_FONT
