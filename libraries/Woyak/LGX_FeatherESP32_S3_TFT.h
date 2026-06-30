#pragma once

#include <LovyanGFX.hpp>

/// <summary>
/// LovyanGFX configuration for the Adafruit Feather ESP32-S3 TFT display.
/// </summary>
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;

public:
   /// <summary>
   /// Initializes bus, panel, and backlight configuration for the onboard TFT.
   /// </summary>
   LGFX()
   {
      // Configure SPI bus.
      {
         auto cfg = _bus_instance.config();
         cfg.spi_host = SPI2_HOST;   // Feather ESP32-S3 SPI host
         cfg.spi_mode = 0;           // SPI mode 0
         cfg.freq_write = 40000000;  // 40 MHz write speed
         cfg.freq_read = 16000000;   // 16 MHz read speed
         cfg.pin_mosi = 35;          // TFT MOSI
         cfg.pin_miso = -1;          // No MISO connection
         cfg.pin_sclk = 36;          // TFT SCK
         cfg.pin_dc = 39;            // TFT D/C
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure ST7789 panel.
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = TFT_CS;      // TFT chip select
         cfg.pin_rst = TFT_RST;    // TFT reset
         cfg.pin_busy = -1;        // Busy pin not used

         cfg.panel_width = 135;    // Native panel width
         cfg.panel_height = 240;   // Native panel height
         cfg.offset_x = 52;        // X offset for this board layout
         cfg.offset_y = 40;        // Y offset for this board layout
         cfg.offset_rotation = 0;  // No extra rotation offset
         cfg.dummy_read_pixel = 16;
         cfg.dummy_read_bits = 1;
         cfg.readable = false;     // Readback not wired
         cfg.invert = true;        // Required for IPS color correctness
         cfg.rgb_order = false;
         cfg.dlen_16bit = false;
         cfg.bus_shared = true;    // SPI bus can be shared

         _panel_instance.config(cfg);
      }

      auto light_cfg = _light_instance.config();
      light_cfg.pin_bl = TFT_BACKLITE;  // Backlight pin
      light_cfg.invert = false;
      _light_instance.config(light_cfg);
      _panel_instance.setLight(&_light_instance);

      setPanel(&_panel_instance);
   }
};
