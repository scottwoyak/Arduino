#pragma once

#include <SPI.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX touch driver for the Hynitron CST328 capacitive touch controller used on the
/// Waveshare ESP32-S3-Touch-LCD-2.8 board (I2C address 0x1A, 16-bit register protocol starting
/// at 0xD000).
/// </summary>
///
class Touch_CST328 : public lgfx::ITouch
{
private:
   static constexpr uint8_t MAX_TOUCH_POINTS = 5;
   static constexpr uint16_t REG_READ_NUMBER = 0xD005;
   static constexpr uint16_t REG_READ_XY = 0xD000;
   static constexpr uint16_t REG_DEBUG_INFO_MODE = 0xD101;
   static constexpr uint16_t REG_NORMAL_MODE = 0xD109;
   static constexpr uint16_t REG_CHECKCODE = 0xD1FC;

   ///
   /// <summary>
   /// Writes a 16-bit big-endian command register with no payload.
   /// </summary>
   /// <param name="reg">16-bit command register address.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _writeCmd(uint16_t reg)
   {
      uint8_t buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
      return lgfx::i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, buf, sizeof(buf), _cfg.freq).has_value();
   }

   ///
   /// <summary>
   /// Writes a 16-bit big-endian register address and a single byte payload.
   /// </summary>
   /// <param name="reg">16-bit register address.</param>
   /// <param name="val">Byte value to write.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _writeReg(uint16_t reg, uint8_t val)
   {
      uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
      return lgfx::i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, buf, sizeof(buf), _cfg.freq).has_value();
   }

   ///
   /// <summary>
   /// Reads bytes from a 16-bit big-endian register address.
   /// </summary>
   /// <param name="reg">16-bit register address.</param>
   /// <param name="data">Destination buffer for read bytes.</param>
   /// <param name="length">Number of bytes to read.</param>
   /// <returns>True if the I2C transaction succeeded.</returns>
   ///
   bool _readReg(uint16_t reg, uint8_t* data, size_t length)
   {
      uint8_t buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
      return lgfx::i2c::transactionWriteRead(_cfg.i2c_port, _cfg.i2c_addr, buf, sizeof(buf), data, length, _cfg.freq).has_value();
   }

public:
   ///
   /// <summary>
   /// Initializes default touch configuration for the CST328 controller.
   /// </summary>
   ///
   Touch_CST328()
   {
      _cfg.i2c_addr = 0x1A;
      _cfg.x_min = 0;
      _cfg.x_max = 239;
      _cfg.y_min = 0;
      _cfg.y_max = 319;
   }

   ///
   /// <summary>
   /// Initializes the CST328 touch controller hardware, reset pulse, and I2C bus.
   /// </summary>
   /// <returns>True if initialization succeeded.</returns>
   ///
   bool init() override
   {
      _inited = false;

      if (_cfg.pin_int >= 0)
      {
         lgfx::pinMode(_cfg.pin_int, lgfx::pin_mode_t::input_pullup);
      }

      if (_cfg.pin_rst >= 0)
      {
         lgfx::pinMode(_cfg.pin_rst, lgfx::pin_mode_t::output);
         lgfx::gpio_hi(_cfg.pin_rst);
         lgfx::delay(50);
         lgfx::gpio_lo(_cfg.pin_rst);
         lgfx::delay(5);
         lgfx::gpio_hi(_cfg.pin_rst);
         lgfx::delay(50);
      }

      lgfx::i2c::init(_cfg.i2c_port, _cfg.pin_sda, _cfg.pin_scl);

      _writeCmd(REG_DEBUG_INFO_MODE);
      lgfx::delay(10);

      uint8_t checkBuf[4] = { 0 };
      _readReg(REG_CHECKCODE, checkBuf, sizeof(checkBuf));

      _writeCmd(REG_NORMAL_MODE);
      _inited = true;
      return true;
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
   /// Reads raw touch points from the CST328 controller.
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

      uint8_t cnt = 0;
      if (!_readReg(REG_READ_NUMBER, &cnt, 1))
      {
         return 0;
      }

      uint8_t points = cnt & 0x0F;
      if (points == 0 || points > MAX_TOUCH_POINTS)
      {
         _writeReg(REG_READ_NUMBER, 0);
         return 0;
      }

      uint8_t buf[27] = { 0 };
      if (!_readReg(REG_READ_XY, buf, sizeof(buf)))
      {
         _writeReg(REG_READ_NUMBER, 0);
         return 0;
      }

      _writeReg(REG_READ_NUMBER, 0);

      if ((buf[0] & 0x0F) != 0x06)
      {
         return 0;
      }

      if (points > count)
      {
         points = count;
      }

      uint8_t offset = 0;
      for (uint_fast8_t i = 0; i < points; i++)
      {
         if (i > 0)
         {
            offset = 2;
         }

         uint8_t idx = i * 5 + offset;
         tp[i].id = i;
         tp[i].x = ((uint16_t)buf[idx + 1] << 4) | ((buf[idx + 3] >> 4) & 0x0F);
         tp[i].y = ((uint16_t)buf[idx + 2] << 4) | (buf[idx + 3] & 0x0F);
         tp[i].size = buf[idx + 4];
      }

      return points;
   }
};

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-2.8 board
/// (2.8" 240x320 ST7789T3 TFT display with CST328 capacitive touch controller).
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   lgfx::Panel_ST7789 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;
   Touch_CST328 _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes SPI bus, ST7789T3 panel, CST328 touch controller, and backlight configuration.
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

      // Configure CST328 capacitive touch controller over I2C (I2C1 on SDA=1, SCL=3).
      {
         auto cfg = _touch_instance.config();
         cfg.x_min = 0;
         cfg.x_max = 239;
         cfg.y_min = 0;
         cfg.y_max = 319;
         cfg.offset_rotation = 0;
         cfg.bus_shared = false;
         cfg.i2c_port = 1;
         cfg.i2c_addr = 0x1A;
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
