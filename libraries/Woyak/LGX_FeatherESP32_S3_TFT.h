#pragma once

//#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI       _bus_instance;
   lgfx::Light_PWM _light_instance;

public:
   LGFX(void)
   {
      // 1. Configure the SPI bus
      {
         auto cfg = _bus_instance.config();
         // SAW cfg.spi_host = FSPI;     // Use FSPI for the Feather ESP32-S3
         cfg.spi_host = SPI2_HOST;     // Use FSPI for the Feather ESP32-S3
         cfg.spi_mode = 0;        // SPI mode 0
         cfg.freq_write = 40000000; // 40 MHz SPI write speed
         cfg.freq_read = 16000000; // 20 MHz SPI read speed
         cfg.pin_mosi = 35;       // Feather ESP32-S3 TFT MOSI pin
         cfg.pin_miso = -1; // 37;       // Feather ESP32-S3 TFT MISO pin
         cfg.pin_sclk = 36;       // Feather ESP32-S3 TFT SCK pin
         cfg.pin_dc = 39;       // Feather ESP32-S3 TFT DC pin
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // 2. Configure the Display Panel (Adafruit 1.14" 240x135 IPS)
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = TFT_CS; // TFT_CS pin
         cfg.pin_rst = TFT_RST; // TFT_RST pin
         cfg.pin_busy = -1; // Busy pin (unused)

         // Adafruit Feather specific resolution and setup
         cfg.panel_width = 135;
         cfg.panel_height = 240;
         cfg.offset_x = 52;
         cfg.offset_y = 40;
         cfg.offset_rotation = 0;
         cfg.dummy_read_pixel = 16;
         cfg.dummy_read_bits = 1;
         cfg.readable = false;
         cfg.invert = true;  // IPS displays typically need this
         cfg.rgb_order = false;
         cfg.dlen_16bit = false;
         cfg.bus_shared = true;

         _panel_instance.config(cfg);
      }

      auto light_cfg = _light_instance.config();
      light_cfg.pin_bl = TFT_BACKLITE;            // Backlight pin
      light_cfg.invert = false;
      _light_instance.config(light_cfg);
      _panel_instance.setLight(&_light_instance);


      setPanel(&_panel_instance);
   }
};
