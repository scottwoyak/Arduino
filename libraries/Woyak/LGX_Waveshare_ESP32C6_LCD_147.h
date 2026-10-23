#pragma once

#include <SPI.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-C6-LCD-1.47 board
/// (1.47" 172x320 ST7789 IPS TFT display).
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;

public:
   ///
   /// <summary>
   /// Initializes SPI bus, ST7789 panel, and backlight configuration.
   /// </summary>
   ///
   LGFX()
   {
      // Configure SPI bus (ESP32-C6 uses SPI2_HOST).
      {
         auto cfg = _bus_instance.config();
         cfg.spi_host = SPI2_HOST;
         cfg.spi_mode = 0;
         cfg.freq_write = 40000000;
         cfg.freq_read = 16000000;
         cfg.spi_3wire = true;
         cfg.use_lock = true;
         cfg.dma_channel = SPI_DMA_CH_AUTO;
         cfg.pin_sclk = 7;
         cfg.pin_mosi = 6;
         cfg.pin_miso = -1;
         cfg.pin_dc = 15;
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure ST7789 IPS panel (172x320 inside 240x320 RAM, 34px horizontal offset).
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = 14;
         cfg.pin_rst = 21;
         cfg.pin_busy = -1;

         cfg.panel_width = 172;
         cfg.panel_height = 320;
         cfg.offset_x = 34;
         cfg.offset_y = 0;
         cfg.offset_rotation = 2; // rotated 180 degrees relative to LovyanGFX's default orientation
         cfg.dummy_read_pixel = 8;
         cfg.dummy_read_bits = 1;
         cfg.readable = false;
         cfg.invert = true;
         cfg.rgb_order = false;
         cfg.dlen_16bit = false;
         cfg.bus_shared = true;

         _panel_instance.config(cfg);
      }

      // Configure backlight (active-high PWM on GPIO22).
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = 22;
         light_cfg.invert = false;
         light_cfg.freq = 44100;
         light_cfg.pwm_channel = 1;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
   }
};
