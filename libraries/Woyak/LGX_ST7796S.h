#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Hardware Pin Definitions
constexpr int8_t PIN_TFT_DC = 6;
constexpr int8_t PIN_TFT_CS = 5;
constexpr int8_t PIN_TFT_RST = 9;
constexpr int8_t PIN_TFT_BL = 10;
constexpr int8_t PIN_TOUCH_CS = 11;
constexpr int8_t PIN_TOUCH_INT = 12;

/* For the Playground Setup
constexpr int8_t PIN_TFT_DC = 8;
constexpr int8_t PIN_TFT_CS = 10;
constexpr int8_t PIN_TFT_RST = 9;
constexpr int8_t PIN_TFT_BL = 7;
constexpr int8_t PIN_TOUCH_CS = 0;
constexpr int8_t PIN_TOUCH_INT = 0;
*/

///
/// <summary>
/// LovyanGFX configuration for the Hosyond ST7796S display.
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7796   _panel_instance;
   lgfx::Bus_SPI        _bus_instance;
   lgfx::Light_PWM      _light_instance;
   //lgfx::Touch_XPT2046  _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes bus, panel, and backlight configuration for the TFT.
   /// </summary>
   ///
   LGFX()
   {

      // --------------------------------------------------------
      // 1. SPI Bus Configuration
      // --------------------------------------------------------
      {
         auto cfg = _bus_instance.config();

         cfg.spi_host = SPI2_HOST;     // FSPI host on the ESP32-S3
         cfg.spi_mode = 0;             // Set SPI mode 0
         cfg.freq_write = 40000000;    // SPI clock for sending data (40MHz)
         cfg.freq_read = 16000000;    // SPI clock for reading data (16MHz)
         cfg.spi_3wire = false;       // We are using standard 4-wire SPI
         cfg.use_lock = true;        // Use transaction locks for a shared bus
         cfg.dma_channel = SPI_DMA_CH_AUTO; // Auto-assign DMA channel

         // Hardware SPI pins (using Arduino IDE macros for your board)
         cfg.pin_sclk = SCK;
         cfg.pin_mosi = MOSI;
         cfg.pin_miso = MISO;
         cfg.pin_dc = PIN_TFT_DC;             // Data/Command

         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // --------------------------------------------------------
      // 2. TFT Panel Configuration (ST7796S)
      // --------------------------------------------------------
      {
         auto cfg = _panel_instance.config();

         cfg.pin_cs = PIN_TFT_CS;             // TFT Chip Select
         cfg.pin_rst = PIN_TFT_RST;             // Hardware Reset
         cfg.pin_busy = -1;            // Not connected

         cfg.panel_width = 320;
         cfg.panel_height = 480;
         cfg.offset_x = 0;
         cfg.offset_y = 0;
         cfg.offset_rotation = 0;
         cfg.dummy_read_pixel = 8;
         cfg.dummy_read_bits = 1;
         cfg.readable = true;  // Allows reading display RAM
         cfg.invert = false; // ST7796 usually doesn't need color inversion
         cfg.rgb_order = false; // false = RGB, true = BGR
         cfg.dlen_16bit = false; // 16-bit data length
         cfg.bus_shared = true;  // CRITICAL: Tells the panel the SPI bus is shared

         _panel_instance.config(cfg);
      }

      // --------------------------------------------------------
      // 3. Touch Controller Configuration (XPT2046)
      // --------------------------------------------------------
      /*
      {
         auto cfg = _touch_instance.config();

         // Raw touch coordinate bounds (Requires calibration in your sketch)
         cfg.x_min = 300;
         cfg.x_max = 3900;
         cfg.y_min = 300;
         cfg.y_max = 3900;

         cfg.pin_int = PIN_TOUCH_INT;          // Touch Interrupt
         cfg.bus_shared = true;        // CRITICAL: Tells the touch controller the SPI bus is shared
         cfg.offset_rotation = 0;

         // Re-declare SPI parameters for the touch instance
         cfg.spi_host = SPI2_HOST;
         cfg.freq = 2500000;           // 2.5MHz is stable for the XPT2046
         cfg.pin_sclk = SCK;
         cfg.pin_mosi = MOSI;
         cfg.pin_miso = MISO;
         cfg.pin_cs = PIN_TOUCH_CS;            // Touch Chip Select

         _touch_instance.config(cfg);
         _panel_instance.setTouch(&_touch_instance);
      }
      */

      // --------------------------------------------------------
      // 4. Backlight Configuration
      // --------------------------------------------------------
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = PIN_TFT_BL;
         light_cfg.invert = false;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      // Apply the complete panel instance to the class
      setPanel(&_panel_instance);
   }
};
