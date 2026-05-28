//                            USER DEFINED SETTINGS
//   Set driver type, fonts to be loaded, pins used and SPI control method etc.
//
//   For VitalScope — ESP32 + 3.5" parallel ILI9486 TFT shield
//
//   IMPORTANT: Replace the existing User_Setup.h in your TFT_eSPI library folder
//   with this file. Usually at:
//     Windows: Documents\Arduino\libraries\TFT_eSPI\User_Setup.h
//     Mac:     ~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
//     Linux:   ~/Arduino/libraries/TFT_eSPI/User_Setup.h

// ##################################################################################
// Section 1. Call up the right driver file and any options for it
// ##################################################################################

#define ILI9486_DRIVER         // This is the right driver for your 3.5" TFT

// ##################################################################################
// Section 2. Define the pins that are used to interface with the display here
// ##################################################################################

// FOR ESP32 PARALLEL 8-BIT TFT (matches your VitalScope wiring exactly)
#define ESP32_PARALLEL

#define TFT_CS   17   // LCD_CS
#define TFT_DC    2   // LCD_RS (a.k.a. Data/Command select)
#define TFT_RST  22   // LCD_RST

#define TFT_WR   15   // LCD_WR (write strobe)
#define TFT_RD   -1   // LCD_RD tied to 3.3V in hardware (not controlled by ESP32)

#define TFT_D0   14   // LCD_D0
#define TFT_D1   27   // LCD_D1
#define TFT_D2   26   // LCD_D2
#define TFT_D3   25   // LCD_D3
#define TFT_D4   33   // LCD_D4
#define TFT_D5   32   // LCD_D5
#define TFT_D6    4   // LCD_D6
#define TFT_D7   13   // LCD_D7

// ##################################################################################
// Section 3. Define the fonts that are to be used here
// ##################################################################################

#define LOAD_GLCD    // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2   // Font 2. Small 16 pixel high font
#define LOAD_FONT4   // Font 4. Medium 26 pixel high font
#define LOAD_FONT6   // Font 6. Large 48 pixel font
#define LOAD_FONT7   // Font 7. 7-segment 48 pixel font
#define LOAD_FONT8   // Font 8. Large 75 pixel font
#define LOAD_GFXFF   // FreeFonts — gives access to lots of custom fonts

#define SMOOTH_FONT

// ##################################################################################
// Section 4. Other options
// ##################################################################################

// Parallel TFTs don't need SPI settings, but these lines prevent compile warnings
#define SPI_FREQUENCY       20000000
#define SPI_READ_FREQUENCY  10000000
#define SPI_TOUCH_FREQUENCY  2500000
