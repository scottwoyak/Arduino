#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

///
/// <summary>
/// LovyanGFX MIPI-DSI panel driver for the ST7701 controller used on the Waveshare
/// ESP32-P4-WIFI6-Touch-LCD-4.3 (480x800 portrait 2-lane DSI panel).
/// </summary>
///
struct Panel_ST7701_Waveshare_43 : public lgfx::Panel_DSI
{
protected:
   const uint8_t* getInitParams(size_t listno) const override
   {
      // Set Command2 Page 0, MADCTL, and COLMOD before vendor init commands (matching esp_lcd_st7701_mipi),
      // followed by the official Waveshare BSP vendor initialization sequence up to SLPOUT.
      static constexpr uint8_t list0[] =
      {
         6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
         2, CMD_MADCTL, 0x00,
         2, CMD_COLMOD, 0x55,
         6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13,
         2, 0xEF, 0x08,
         6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10,
         3, 0xC0, 0x63, 0x00,
         3, 0xC1, 0x0D, 0x02,
         3, 0xC2, 0x17, 0x08,
         2, 0xCC, 0x10,
         17, 0xB0, 0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69,
         17, 0xB1, 0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55,
         6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
         2, 0xB0, 0x5D,
         2, 0xB1, 0x4E,
         2, 0xB2, 0x87,
         2, 0xB3, 0x80,
         2, 0xB5, 0x4E,
         2, 0xB7, 0x85,
         2, 0xB8, 0x21,
         3, 0xB9, 0x10, 0x1F,
         2, 0xBB, 0x03,
         2, 0xBC, 0x00,
         2, 0xC1, 0x78,
         2, 0xC2, 0x78,
         2, 0xD0, 0x88,
         4, 0xE0, 0x00, 0x3A, 0x02,
         12, 0xE1, 0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40,
         14, 0xE2, 0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00,
         5, 0xE3, 0x00, 0x00, 0x33, 0x33,
         3, 0xE4, 0x44, 0x44,
         17, 0xE5, 0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0,
         5, 0xE6, 0x00, 0x00, 0x33, 0x33,
         3, 0xE7, 0x44, 0x44,
         17, 0xE8, 0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0,
         8, 0xEB, 0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00,
         3, 0xEC, 0x08, 0x01,
         17, 0xED, 0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B,
         7, 0xEF, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
         6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
         1, CMD_SLPOUT,
         0,
      };

      // Display ON after 120 ms SLPOUT delay.
      static constexpr uint8_t list1[] =
      {
         1, CMD_DISPON,
         0,
      };

      switch (listno)
      {
      case 0:
         return list0;
      case 1:
         return list1;
      default:
         return nullptr;
      }
   }

   size_t getInitDelay(size_t listno) const override
   {
      switch (listno)
      {
      case 0:
         return 120;
      default:
         return 0;
      }
   }
};

///
/// <summary>
/// LovyanGFX display configuration for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 board.
/// Hardware: 480x800 ST7701 MIPI-DSI (2-lane) IPS display with GT911 I2C capacitive touch
/// controller and active-low PWM backlight on GPIO26.
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   static constexpr uint8_t I2C_PORT = 0;
   static constexpr uint32_t I2C_FREQ = 400000;
   static constexpr int8_t PIN_SDA = 7;
   static constexpr int8_t PIN_SCL = 8;
   static constexpr int8_t PIN_TOUCH_RST = 23;
   static constexpr int8_t PIN_LCD_BL = 26;
   static constexpr int8_t PIN_LCD_RST = 27;

   lgfx::Bus_DSI _bus_instance;
   Panel_ST7701_Waveshare_43 _panel_instance;
   lgfx::Light_PWM _light_instance;
   lgfx::Touch_GT911 _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes MIPI-DSI bus, ST7701 panel, GT911 touch controller, and backlight configuration.
   /// </summary>
   ///
   LGFX()
   {
      // Configure 2-lane MIPI-DSI bus (500 Mbps per lane, 2.5V PHY powered from LDO channel 3).
      {
         auto cfg = _bus_instance.config();
         cfg.bus_id = 0;
         cfg.lane_num = 2;
         cfg.lane_mbps = 500;
         cfg.ldo_chan_id = 3;
         cfg.ldo_voltage_mv = 2500;
         cfg.lcd_cmd_bits = 8;
         cfg.lcd_param_bits = 8;
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure ST7701 panel (480x800 portrait native).
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = -1;
         cfg.pin_rst = PIN_LCD_RST;
         cfg.pin_busy = -1;
         cfg.memory_width = 480;
         cfg.memory_height = 800;
         cfg.panel_width = 480;
         cfg.panel_height = 800;
         cfg.offset_x = 0;
         cfg.offset_y = 0;
         cfg.offset_rotation = 2; // rotated 180 degrees relative to LovyanGFX's default orientation
         cfg.readable = false;
         cfg.invert = false;
         cfg.rgb_order = true;
         cfg.dlen_16bit = false;
         cfg.bus_shared = false;
         _panel_instance.config(cfg);
      }

      // Configure MIPI-DSI DPI timing (30 MHz pixel clock).
      {
         auto det = _panel_instance.config_detail();
         det.dpi_freq_mhz = 30;
         det.hsync_back_porch = 42;
         det.hsync_pulse_width = 12;
         det.hsync_front_porch = 42;
         det.vsync_back_porch = 2;
         det.vsync_pulse_width = 8;
         det.vsync_front_porch = 60;
         _panel_instance.config_detail(det);
      }

      // Configure GT911 capacitive touch controller over I2C.
      {
         auto cfg = _touch_instance.config();
         cfg.x_min = 0;
         cfg.x_max = 479;
         cfg.y_min = 0;
         cfg.y_max = 799;
         cfg.offset_rotation = 0;
         cfg.bus_shared = false;
         cfg.i2c_port = I2C_PORT;
         cfg.i2c_addr = 0x5D;
         cfg.pin_sda = PIN_SDA;
         cfg.pin_scl = PIN_SCL;
         cfg.pin_int = -1;
         cfg.pin_rst = PIN_TOUCH_RST;
         cfg.freq = I2C_FREQ;
         _touch_instance.config(cfg);
         _panel_instance.setTouch(&_touch_instance);
      }

      // Configure backlight (active-low PWM on GPIO26 at 5 kHz, matching the official BSP).
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = PIN_LCD_BL;
         light_cfg.invert = true;
         light_cfg.freq = 5000;
         light_cfg.pwm_channel = 7;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
   }
};
