#pragma once

#include <SPI.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX touch driver for the Hynitron CST3530 capacitive touch controller used on the
/// Waveshare ESP32-S3-Touch-LCD-2.8 V2 board (I2C address 0x58, 32-bit big-endian register
/// protocol). Uses Arduino's TwoWire (Wire1) so it works cleanly with ESP-IDF 5.x's new I2C
/// master driver and peripheral manager.
/// </summary>
///
class Touch_CST3530 : public lgfx::ITouch
{
private:
   static constexpr uint8_t MAX_TOUCH_POINTS = 5;
   static constexpr uint32_t REG_END_READ = 0xD00002AB;
   static constexpr uint32_t REG_DATA = 0xD0070000;
   static constexpr uint32_t REG_COORD_NEXT = 0xD0070900;
   static constexpr uint32_t RELEASE_DEBOUNCE_MS = 30;

   uint32_t _lastTouchMs = 0;
   bool _hasLastTouch = false;
   lgfx::touch_point_t _lastPoint;

   ///
   /// <summary>
   /// Gets the Arduino TwoWire instance corresponding to the configured I2C port.
   /// </summary>
   /// <returns>Pointer to Wire or Wire1.</returns>
   ///
   TwoWire* _wire() const
   {
	  return (_cfg.i2c_port == 0) ? &Wire : &Wire1;
   }

   ///
   /// <summary>
   /// Writes a 32-bit big-endian register address.
   /// </summary>
   /// <param name="reg">32-bit register address.</param>
   ///
   void _writeRegAddress(uint32_t reg)
   {
	  TwoWire* w = _wire();
	  w->write((uint8_t)(reg >> 24));
	  w->write((uint8_t)(reg >> 16));
	  w->write((uint8_t)(reg >> 8));
	  w->write((uint8_t)reg);
   }

   ///
   /// <summary>
   /// Writes a 32-bit big-endian command register with no payload.
   /// </summary>
   /// <param name="reg">32-bit command register address.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _writeCmd(uint32_t reg)
   {
	  TwoWire* w = _wire();
	  w->beginTransmission(_cfg.i2c_addr);
	  _writeRegAddress(reg);
	  return w->endTransmission(true) == 0;
   }

   ///
   /// <summary>
   /// Reads bytes from a 32-bit big-endian register address.
   /// </summary>
   /// <param name="reg">32-bit register address.</param>
   /// <param name="data">Destination buffer for read bytes.</param>
   /// <param name="length">Number of bytes to read.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _readReg(uint32_t reg, uint8_t* data, size_t length)
   {
	  TwoWire* w = _wire();
	  w->beginTransmission(_cfg.i2c_addr);
	  _writeRegAddress(reg);
	  if (w->endTransmission(false) != 0)
	  {
		 return false;
	  }

	  if (w->requestFrom((uint8_t)_cfg.i2c_addr, length) != length)
	  {
		 return false;
	  }

	  for (size_t i = 0; i < length; i++)
	  {
		 data[i] = (uint8_t)w->read();
	  }

	  return true;
   }

public:
   ///
   /// <summary>
   /// Initializes default touch configuration for the CST3530 controller.
   /// </summary>
   ///
   Touch_CST3530()
   {
	  _cfg.i2c_addr = 0x58;
	  _cfg.x_min = 0;
	  _cfg.x_max = 239;
	  _cfg.y_min = 0;
	  _cfg.y_max = 319;
   }

   ///
   /// <summary>
   /// Initializes the CST3530 touch controller hardware, reset pulse, and I2C bus.
   /// </summary>
   /// <returns>True if initialization succeeded.</returns>
   ///
   bool init() override
   {
	  _inited = false;

	  _wire()->begin(_cfg.pin_sda, _cfg.pin_scl, _cfg.freq);

	  if (_cfg.pin_int >= 0)
	  {
		 pinMode(_cfg.pin_int, INPUT);
	  }

	  if (_cfg.pin_rst >= 0)
	  {
		 pinMode(_cfg.pin_rst, OUTPUT);
		 digitalWrite(_cfg.pin_rst, LOW);
		 delay(100);
		 digitalWrite(_cfg.pin_rst, HIGH);
		 delay(500);
	  }

	  _wire()->beginTransmission(_cfg.i2c_addr);
	  _inited = _wire()->endTransmission(true) == 0;
	  return _inited;
   }

   ///
   /// <summary>
   /// Wakes the touch controller from sleep.
   /// </summary>
   ///
   void wakeup() override {}

   ///
   /// <summary>
   /// Puts the touch controller into sleep mode.
   /// </summary>
   ///
   void sleep() override {}

   ///
   /// <summary>
   /// Reads raw touch points from the CST3530 controller.
   /// </summary>
   /// <param name="tp">Buffer to receive touch points.</param>
   /// <param name="count">Maximum number of touch points to read.</param>
   /// <returns>Number of active touch points read.</returns>
   ///
   uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override
   {
	  if (!_inited && !init())
	  {
		 return 0;
	  }

	  if (count == 0)
	  {
		 return 0;
	  }

	  uint32_t now = millis();

	  // Report the last touch for a short time instead of a spurious release between frames.
	  auto noNewTouch = [&]() -> uint_fast8_t
	  {
		 if (_hasLastTouch && (now - _lastTouchMs) < RELEASE_DEBOUNCE_MS)
		 {
			tp[0] = _lastPoint;
			return 1;
		 }
		 _hasLastTouch = false;
		 return 0;
	  };

	  uint8_t buf[9 + (MAX_TOUCH_POINTS - 1) * 5] = { 0 };
	  if (!_readReg(REG_DATA, buf, 9))
	  {
		 return noNewTouch();
	  }

	  uint8_t points = buf[3] & 0x0F;
	  if (points == 0 || (buf[8] & 0xF0) == 0 || points > MAX_TOUCH_POINTS)
	  {
		 _writeCmd(REG_END_READ);
		 return noNewTouch();
	  }

	  if (points > 1 && !_readReg(REG_COORD_NEXT, &buf[9], (points - 1) * 5))
	  {
		 _writeCmd(REG_END_READ);
		 return noNewTouch();
	  }

	  _writeCmd(REG_END_READ);

	  if (points > count)
	  {
		 points = count;
	  }

	  for (uint_fast8_t i = 0; i < points; i++)
	  {
		 uint8_t idx = i * 5;
		 tp[i].id = i;
		 tp[i].x = ((buf[idx + 7] & 0x0F) << 8) + buf[idx + 4];
		 tp[i].y = ((buf[idx + 7] & 0xF0) << 4) + buf[idx + 5];
		 tp[i].size = buf[idx + 6];
	  }

	  _lastPoint = tp[0];
	  _lastTouchMs = now;
	  _hasLastTouch = true;

	  return points;
   }
};

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-2.8 V2 board
/// (2.8" 240x320 ST7789T3 TFT display with CST3530 capacitive touch controller).
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;
   Touch_CST3530 _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes SPI bus, ST7789T3 panel, CST3530 touch controller, and backlight configuration.
   /// </summary>
   ///
   LGFX()
   {
	  // Configure SPI bus.
	  {
		 auto cfg = _bus_instance.config();
		 cfg.spi_host = SPI2_HOST;
		 cfg.spi_mode = 0;
		 cfg.freq_write = 40000000;
		 cfg.freq_read = 16000000;
		 cfg.spi_3wire = false;
		 cfg.use_lock = true;
		 cfg.dma_channel = SPI_DMA_CH_AUTO;
		 cfg.pin_sclk = 40;
		 cfg.pin_mosi = 45;
		 cfg.pin_miso = -1;
		 cfg.pin_dc = 41;
		 _bus_instance.config(cfg);
		 _panel_instance.setBus(&_bus_instance);
	  }

	  // Configure ST7789T3 panel (240x320).
	  {
		 auto cfg = _panel_instance.config();
		 cfg.pin_cs = 42;
		 cfg.pin_rst = 39;
		 cfg.pin_busy = -1;

		 cfg.panel_width = 240;
		 cfg.panel_height = 320;
		 cfg.offset_x = 0;
		 cfg.offset_y = 0;
		 cfg.offset_rotation = 0;
		 cfg.dummy_read_pixel = 8;
		 cfg.dummy_read_bits = 1;
		 cfg.readable = false;
		 cfg.invert = true;
		 cfg.rgb_order = false;
		 cfg.dlen_16bit = false;
		 cfg.bus_shared = false;

		 _panel_instance.config(cfg);
	  }

	  // Configure CST3530 capacitive touch controller over I2C (I2C1 on SDA=1, SCL=3).
	  {
		 auto cfg = _touch_instance.config();
		 cfg.x_min = 0;
		 cfg.x_max = 239;
		 cfg.y_min = 0;
		 cfg.y_max = 319;
		 cfg.offset_rotation = 0;
		 cfg.bus_shared = false;
		 cfg.i2c_port = 1;
		 cfg.i2c_addr = 0x58;
		 cfg.pin_sda = 1;
		 cfg.pin_scl = 3;
		 cfg.pin_int = 4;
		 cfg.pin_rst = 2;
		 cfg.freq = 400000;

		 _touch_instance.config(cfg);
		 _panel_instance.setTouch(&_touch_instance);
	  }

	  // Configure backlight (active-high on GPIO5).
	  {
		 auto light_cfg = _light_instance.config();
		 light_cfg.pin_bl = 5;
		 light_cfg.invert = false;
		 _light_instance.config(light_cfg);
		 _panel_instance.setLight(&_light_instance);
	  }

	  setPanel(&_panel_instance);
   }
};
