#pragma once

#include <SPI.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-1.9 board
/// (1.9" 170x320 ST7789V2 TFT display with CST816S capacitive touch controller).
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
   /// Initializes SPI bus, ST7789V2 panel, CST816S touch controller, and backlight configuration.
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
		 cfg.spi_3wire = false;
		 cfg.use_lock = true;
		 cfg.dma_channel = SPI_DMA_CH_AUTO;
		 cfg.pin_sclk = 10;
		 cfg.pin_mosi = 13;
		 cfg.pin_miso = -1;
		 cfg.pin_dc = 11;
		 _bus_instance.config(cfg);
		 _panel_instance.setBus(&_bus_instance);
	  }

	  // Configure ST7789V2 panel (170x320 inside 240x320 RAM, 35px horizontal offset).
	  {
		 auto cfg = _panel_instance.config();
		 cfg.pin_cs = 12;
		 cfg.pin_rst = 9;
		 cfg.pin_busy = -1;

		 cfg.panel_width = 170;
		 cfg.panel_height = 320;
		 cfg.offset_x = 35;
		 cfg.offset_y = 0;
		 cfg.offset_rotation = 2; // rotated 180 degrees relative to LovyanGFX's default orientation
		 cfg.dummy_read_pixel = 8;
		 cfg.dummy_read_bits = 1;
		 cfg.readable = false;
		 cfg.invert = true;
		 cfg.rgb_order = false;
		 cfg.dlen_16bit = false;
		 cfg.bus_shared = false;

		 _panel_instance.config(cfg);
	  }

	  // Configure CST816S capacitive touch controller over I2C.
	  {
		 auto cfg = _touch_instance.config();
		 cfg.x_min = 0;
		 cfg.x_max = 169;
		 cfg.y_min = 0;
		 cfg.y_max = 319;
		 cfg.offset_rotation = 0;
		 cfg.bus_shared = false;
		 cfg.i2c_port = 0;
		 cfg.i2c_addr = 0x15;
		 cfg.pin_sda = 47;
		 cfg.pin_scl = 48;
		 cfg.pin_int = -1;
		 cfg.pin_rst = -1;
		 cfg.freq = 400000;

		 _touch_instance.config(cfg);
		 _panel_instance.setTouch(&_touch_instance);
	  }

	  // Configure backlight (active-low on GPIO14).
	  {
		  auto light_cfg = _light_instance.config();
		  light_cfg.pin_bl = 14;
		  light_cfg.invert = true;
		  _light_instance.config(light_cfg);
		  _panel_instance.setLight(&_light_instance);
	  }

	  setPanel(&_panel_instance);
   }
};
