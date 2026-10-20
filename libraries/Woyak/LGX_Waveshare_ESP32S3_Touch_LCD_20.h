#pragma once

#include <SPI.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-2 board
/// (2.0" 240x320 ST7789T3 IPS TFT display with CST816S/CST816D capacitive touch controller).
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;
   lgfx::Touch_CST816S _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes SPI bus, ST7789T3 panel, CST816S touch controller, and backlight configuration.
   /// </summary>
   ///
   LGFX()
   {
      // Configure SPI bus.
      {
         auto cfg = _bus_instance.config();
         cfg.spi_host = SPI3_HOST;
         cfg.spi_mode = 0;
         cfg.freq_write = 40000000;
         cfg.freq_read = 16000000;
         cfg.spi_3wire = true;
         cfg.use_lock = true;
         cfg.dma_channel = SPI_DMA_CH_AUTO;
         cfg.pin_sclk = 39;
         cfg.pin_mosi = 38;
         cfg.pin_miso = 40;
         cfg.pin_dc = 42;
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure ST7789T3 IPS panel (240x320).
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = 45;
         cfg.pin_rst = -1;
         cfg.pin_busy = -1;

         cfg.panel_width = 240;
         cfg.panel_height = 320;
         cfg.offset_x = 0;
         cfg.offset_y = 0;
         cfg.offset_rotation = 0;
         cfg.dummy_read_pixel = 8;
         cfg.dummy_read_bits = 1;
         cfg.readable = true;
         cfg.invert = true;
         cfg.rgb_order = false;
         cfg.dlen_16bit = false;
         cfg.bus_shared = true;

         _panel_instance.config(cfg);
      }

      // Configure CST816S/CST816D capacitive touch controller over I2C.
      {
         auto cfg = _touch_instance.config();
         cfg.x_min = 0;
         cfg.x_max = 239;
         cfg.y_min = 0;
         cfg.y_max = 319;
         cfg.offset_rotation = 0;
         cfg.bus_shared = false;
         cfg.i2c_port = 0;
         cfg.i2c_addr = 0x15;
         cfg.pin_sda = 48;
         cfg.pin_scl = 47;
         cfg.pin_int = -1;
         cfg.pin_rst = -1;
         cfg.freq = 400000;

         _touch_instance.config(cfg);
         _panel_instance.setTouch(&_touch_instance);
      }

      // Configure backlight (active-high PWM on GPIO1).
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = 1;
         light_cfg.invert = false;
         light_cfg.freq = 44100;
         light_cfg.pwm_channel = 7;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
   }
};
