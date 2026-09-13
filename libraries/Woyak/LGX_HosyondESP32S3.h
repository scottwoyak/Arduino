#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
   lgfx::Panel_ST7796 _panel_instance;
   lgfx::Bus_SPI       _bus_instance;
   lgfx::Light_PWM _light_instance;
   lgfx::Touch_FT5x06 _touch_instance; // FT6336U capacitive touch controller

public:

   LGFX(void)
   {
     {
       auto cfg = _bus_instance.config();
       cfg.spi_host = SPI2_HOST;
       cfg.freq_write = 40000000; // ST7796 panel supports much faster writes than the default; speeds up full-screen fills
       cfg.freq_read = 16000000;
       cfg.pin_mosi = 11;
       cfg.pin_miso = 13;
       cfg.pin_sclk = 12;
       cfg.pin_dc = 46;
       _bus_instance.config(cfg);
       _panel_instance.setBus(&_bus_instance);

     }

     {
       auto cfg = _panel_instance.config();
       cfg.pin_cs = 10;
       cfg.pin_rst = -1; // tied to the board's EN/CHIP_PU pin
       cfg.pin_busy = -1;
       cfg.panel_width = 320;
       cfg.panel_height = 480;
       cfg.offset_rotation = 2; // this board is mounted upside-down relative to LovyanGFX's default orientation
       cfg.invert = true; // this panel reports inverted colors relative to LovyanGFX's default
       cfg.readable = false; // not used; avoids SPI read transactions, which were more prone to corruption than writes during OTA flash writes

       _panel_instance.config(cfg);
     }

     {
      // FT6336U capacitive touch controller, wired over I2C. Pins are per the board's
      // datasheet and may need correcting once the actual board is tested.
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.offset_rotation = 0; // touch is already correctly oriented; matching the panel's offset_rotation (2) double-flipped both axes
      cfg.bus_shared = false;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 16;
      cfg.pin_scl = 15;
      cfg.pin_int = 17;
      cfg.freq = 400000;

      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
     }

     auto light_cfg = _light_instance.config();
     light_cfg.pin_bl = 45;            // Backlight pin
     light_cfg.invert = false;
     _light_instance.config(light_cfg);
     _panel_instance.setLight(&_light_instance);

     setPanel(&_panel_instance);
   }
};
