#pragma once

#include <SPI.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

#include "I2CMutex.h"

///
/// <summary>
/// LovyanGFX touch driver for the FocalTech FT6336U capacitive touch controller used on
/// the Hosyond ESP32-S3 4.0" board (I2C address 0x38). Uses Arduino's TwoWire so it shares
/// the I2C bus cleanly with ESP-IDF 5.x's I2C master driver and external I2C sensors (like ADS1115),
/// and checks the hardware INT pin (active-low) before initiating I2C transactions.
/// </summary>
///
class Touch_FT6336U_Wire : public lgfx::ITouch
{
private:
   static constexpr uint8_t MAX_TOUCH_POINTS = 2;
   static constexpr uint8_t REG_TD_STATUS = 0x02;

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
   /// Reads bytes from an 8-bit register address.
   /// </summary>
   /// <param name="reg">8-bit register address.</param>
   /// <param name="data">Destination buffer for read bytes.</param>
   /// <param name="length">Number of bytes to read.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _readReg(uint8_t reg, uint8_t* data, size_t length)
   {
      I2CScopedLock lock;
      TwoWire* w = _wire();
      w->beginTransmission(_cfg.i2c_addr);
      w->write(reg);
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
   /// Initializes default touch configuration for the FT6336U controller.
   /// </summary>
   ///
   Touch_FT6336U_Wire()
   {
      _cfg.i2c_addr = 0x38;
      _cfg.x_min = 0;
      _cfg.x_max = 319;
      _cfg.y_min = 0;
      _cfg.y_max = 479;
   }

   ///
   /// <summary>
   /// Initializes the FT6336U touch controller and I2C bus.
   /// </summary>
   /// <returns>True if initialization succeeded.</returns>
   ///
   bool init() override
   {
      _inited = false;

      _wire()->begin(_cfg.pin_sda, _cfg.pin_scl, _cfg.freq);

      if (_cfg.pin_int >= 0)
      {
         pinMode(_cfg.pin_int, INPUT_PULLUP);
      }

      if (_cfg.pin_rst >= 0)
      {
         pinMode(_cfg.pin_rst, OUTPUT);
         digitalWrite(_cfg.pin_rst, LOW);
         delay(10);
         digitalWrite(_cfg.pin_rst, HIGH);
         delay(50);
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
   /// Reads raw touch points from the FT6336U controller.
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

      // Skip I2C bus traffic entirely when the active-low touch interrupt pin is idle (HIGH).
      if (_cfg.pin_int >= 0 && digitalRead(_cfg.pin_int) != LOW)
      {
         return 0;
      }

      uint8_t status = 0;
      if (!_readReg(REG_TD_STATUS, &status, 1))
      {
         return 0;
      }

      uint8_t points = status & 0x0F;
      if (points == 0 || points > MAX_TOUCH_POINTS)
      {
         return 0;
      }

      uint8_t buf[MAX_TOUCH_POINTS * 6] = { 0 };
      if (!_readReg(REG_TD_STATUS + 1, buf, points * 6))
      {
         return 0;
      }

      if (points > count)
      {
         points = count;
      }

      for (uint_fast8_t i = 0; i < points; i++)
      {
         uint8_t idx = i * 6;
         tp[i].x = ((buf[idx] & 0x0F) << 8) | buf[idx + 1];
         tp[i].y = ((buf[idx + 2] & 0x0F) << 8) | buf[idx + 3];
         tp[i].id = buf[idx + 2] >> 4;
         tp[i].size = 1;
      }

      return points;
   }
};

class LGFX : public lgfx::LGFX_Device
{
   lgfx::Panel_ST7796 _panel_instance;
   lgfx::Bus_SPI       _bus_instance;
   lgfx::Light_PWM _light_instance;
   Touch_FT6336U_Wire _touch_instance; // FT6336U capacitive touch controller over Arduino Wire

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
      // FT6336U capacitive touch controller, wired over I2C.
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.offset_rotation = 0; // touch is already correctly oriented; matching the panel's offset_rotation (2) double-flipped both axes
      cfg.bus_shared = true;
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = 16;
      cfg.pin_scl = 15;
      cfg.pin_int = 17;
      cfg.pin_rst = -1;
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
