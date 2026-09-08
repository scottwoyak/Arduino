#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
	lgfx::Panel_ST7796 _panel_instance;
	lgfx::Bus_SPI       _bus_instance;
	lgfx::Light_PWM _light_instance;
	lgfx::Touch_XPT2046 _touch_instance;

public:

	LGFX(void)
	{
	  {
		 auto cfg = _bus_instance.config();
		 cfg.spi_host = VSPI_HOST;
		 cfg.freq_write = 40000000; // ST7796 panel supports much faster writes than the default; speeds up full-screen fills
		 cfg.freq_read = 16000000;
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
		 cfg.offset_rotation = 2; // this board is mounted upside-down relative to LovyanGFX's default orientation

		 _panel_instance.config(cfg);
	  }

	  {
		// XPT2046 resistive touch controller, wired over SPI (shares the display's SPI
		// bus). Pins are common Hosyond ESP32-32E defaults and may need correcting for
		// the actual board. Unlike the GT911 (capacitive) touch controller, XPT2046 is
		// resistive and reports raw 12-bit ADC values, not pixel coordinates, so
		// x_min/x_max/y_min/y_max must be the raw ADC range read at the physical
		// corners of the touch panel, not the display's pixel dimensions. Calibrated
		// from observed raw readings (X: ~200-3825, Y: ~30 down to ~-3425). Note the
		// touch's offset_rotation is intentionally 0 (not matching the panel's own
		// offset_rotation of 2): verified via raw touch data that the calibration
		// below already aligns raw ADC to pixel space correctly, so no extra rotation
		// flip is needed here - applying one (as previously tried) mirrors X instead.
		auto cfg = _touch_instance.config();
		cfg.x_min = 200;
		cfg.x_max = 3825;
		cfg.y_min = 3425;
		cfg.y_max = 30;
		cfg.offset_rotation = 0;
		cfg.bus_shared = true;
		cfg.spi_host = VSPI_HOST;
		cfg.freq = 1000000;
		cfg.pin_sclk = 14;
		cfg.pin_mosi = 13;
		cfg.pin_miso = 12;
		cfg.pin_cs = 33;
		cfg.pin_int = 21;

		_touch_instance.config(cfg);
		_panel_instance.setTouch(&_touch_instance);
	  }

	  auto light_cfg = _light_instance.config();
	  light_cfg.pin_bl = 27;            // Backlight pin
	  light_cfg.invert = false;
	  _light_instance.config(light_cfg);
	  _panel_instance.setLight(&_light_instance);

	  setPanel(&_panel_instance);
	}
};
