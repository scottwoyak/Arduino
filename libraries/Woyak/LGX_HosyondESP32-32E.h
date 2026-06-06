#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
   lgfx::Panel_ST7796 _panel_instance;
   lgfx::Bus_SPI       _bus_instance;
   lgfx::Light_PWM _light_instance;

public:

   LGFX(void)
   {
	  {
		 auto cfg = _bus_instance.config();
		 cfg.spi_host = VSPI_HOST;
		 cfg.pin_mosi = 13;
		 cfg.pin_miso = 12;
		 cfg.pin_sclk = 14;
		 cfg.pin_dc = 2;
		 _bus_instance.config(cfg);
		 _panel_instance.setBus(&_bus_instance);

	  }

	  {
		 auto cfg = _panel_instance.config();
		 cfg.pin_cs = 15;
		 cfg.pin_rst = -1;
		 cfg.pin_busy = -1;
		 cfg.panel_width = 320;
		 cfg.panel_height = 480;

		 _panel_instance.config(cfg);
	  }

	  auto light_cfg = _light_instance.config();
	  light_cfg.pin_bl = 27;            // Backlight pin
	  light_cfg.invert = false;
	  _light_instance.config(light_cfg);
	  _panel_instance.setLight(&_light_instance);

	  setPanel(&_panel_instance);
   }
};
