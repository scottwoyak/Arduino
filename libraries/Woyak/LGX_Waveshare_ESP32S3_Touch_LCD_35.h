#pragma once

#include <SPI.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

///
/// <summary>
/// LovyanGFX touch driver for the FocalTech FT6336U capacitive touch controller used on
/// the Waveshare ESP32-S3-Touch-LCD-3.5 (Rev 2.0) board (I2C address 0x38). Uses Arduino's
/// TwoWire so it works cleanly with ESP-IDF 5.x's I2C master driver and peripheral manager.
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

///
/// <summary>
/// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-3.5 (Rev 2.0) board
/// (3.5" 320x480 ST7796S IPS TFT display over SPI with FT6336U capacitive touch
/// controller and TCA9554 I/O expander for LCD reset).
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   static constexpr uint8_t I2C_PORT = 0;
   static constexpr uint8_t PIN_SDA = 8;
   static constexpr uint8_t PIN_SCL = 7;
   static constexpr uint32_t I2C_FREQ = 400000;

   // TCA9554 I/O expander on the shared I2C bus (GPIO8/GPIO7).
   // Pin EXIO1 drives the ST7796S LCD reset line (active-low).
   static constexpr uint8_t TCA9554_I2C_ADDR = 0x20;
   static constexpr uint8_t TCA9554_OUTPUT_PORT_REG = 0x01;
   static constexpr uint8_t TCA9554_CONFIG_PORT_REG = 0x03;
   static constexpr uint8_t TCA9554_LCD_RST_BIT = (1 << 1);

   lgfx::Panel_ST7796 _panel_instance;
   lgfx::Bus_SPI _bus_instance;
   lgfx::Light_PWM _light_instance;
   Touch_FT6336U_Wire _touch_instance;

   ///
   /// <summary>
   /// Reads a single 8-bit register from the TCA9554 I/O expander over Wire.
   /// </summary>
   /// <param name="reg">Register address to read.</param>
   /// <param name="value">Receives the register byte when successful.</param>
   /// <returns>True if the read succeeded; otherwise, false.</returns>
   ///
   static bool _readTCA9554Reg(uint8_t reg, uint8_t* value)
   {
      Wire.beginTransmission(TCA9554_I2C_ADDR);
      Wire.write(reg);
      if (Wire.endTransmission(false) != 0)
      {
         return false;
      }

      if (Wire.requestFrom(TCA9554_I2C_ADDR, (uint8_t)1) != 1)
      {
         return false;
      }

      *value = (uint8_t)Wire.read();
      return true;
   }

   ///
   /// <summary>
   /// Writes a single 8-bit register on the TCA9554 I/O expander over Wire.
   /// </summary>
   /// <param name="reg">Register address to write.</param>
   /// <param name="value">Register value to write.</param>
   /// <returns>True if the write succeeded; otherwise, false.</returns>
   ///
   static bool _writeTCA9554Reg(uint8_t reg, uint8_t value)
   {
      Wire.beginTransmission(TCA9554_I2C_ADDR);
      Wire.write(reg);
      Wire.write(value);
      return Wire.endTransmission(true) == 0;
   }

   ///
   /// <summary>
   /// Toggles the ST7796S hardware reset pin (EXIO1 on the onboard TCA9554 I/O expander)
   /// before panel initialization, matching the official Waveshare demo sequence.
   /// </summary>
   ///
   void _resetPanelViaTCA9554()
   {
      Wire.begin(PIN_SDA, PIN_SCL, I2C_FREQ);

      uint8_t outReg = 0xFF;
      _readTCA9554Reg(TCA9554_OUTPUT_PORT_REG, &outReg);

      uint8_t cfgReg = 0xFF;
      _readTCA9554Reg(TCA9554_CONFIG_PORT_REG, &cfgReg);

      // Configure EXIO1 as an output (0 = output, 1 = input) while leaving all other
      // expander pins untouched, then pulse EXIO1 HIGH -> LOW -> HIGH to reset the LCD.
      outReg |= TCA9554_LCD_RST_BIT;
      _writeTCA9554Reg(TCA9554_OUTPUT_PORT_REG, outReg);
      cfgReg &= ~TCA9554_LCD_RST_BIT;
      _writeTCA9554Reg(TCA9554_CONFIG_PORT_REG, cfgReg);
      delay(10);

      outReg &= ~TCA9554_LCD_RST_BIT;
      _writeTCA9554Reg(TCA9554_OUTPUT_PORT_REG, outReg);
      delay(10);

      outReg |= TCA9554_LCD_RST_BIT;
      _writeTCA9554Reg(TCA9554_OUTPUT_PORT_REG, outReg);
      delay(200);
   }

public:
   ///
   /// <summary>
   /// Initializes SPI bus, ST7796S panel, FT6336U touch controller, and backlight configuration.
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
         cfg.pin_sclk = 5;
         cfg.pin_mosi = 1;
         cfg.pin_miso = 2;
         cfg.pin_dc = 3;
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure ST7796S IPS panel (320x480).
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = -1;
         cfg.pin_rst = -1; // Reset is driven via TCA9554 EXIO1 in init_impl().
         cfg.pin_busy = -1;

         cfg.panel_width = 320;
         cfg.panel_height = 480;
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

      // Configure FT6336U capacitive touch controller over I2C.
      {
         auto cfg = _touch_instance.config();
         cfg.x_min = 0;
         cfg.x_max = 319;
         cfg.y_min = 0;
         cfg.y_max = 479;
         cfg.offset_rotation = 0;
         cfg.bus_shared = false;
         cfg.i2c_port = I2C_PORT;
         cfg.i2c_addr = 0x38;
         cfg.pin_sda = PIN_SDA;
         cfg.pin_scl = PIN_SCL;
         cfg.pin_int = -1;
         cfg.pin_rst = -1;
         cfg.freq = I2C_FREQ;

         _touch_instance.config(cfg);
         _panel_instance.setTouch(&_touch_instance);
      }

      // Configure backlight (active-high PWM on GPIO6).
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = 6;
         light_cfg.invert = false;
         light_cfg.freq = 5000;
         light_cfg.pwm_channel = 7;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
   }

   ///
   /// <summary>
   /// Resets the ST7796S panel via the TCA9554 I/O expander before running standard
   /// LovyanGFX initialization.
   /// </summary>
   /// <param name="use_reset">True to perform hardware reset prior to panel init.</param>
   /// <param name="use_clear">True to clear the screen after initialization.</param>
   /// <returns>True if initialization succeeded; otherwise, false.</returns>
   ///
   bool init_impl(bool use_reset, bool use_clear) override
   {
      if (use_reset)
      {
         _resetPanelViaTCA9554();
      }

      return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
   }
};
