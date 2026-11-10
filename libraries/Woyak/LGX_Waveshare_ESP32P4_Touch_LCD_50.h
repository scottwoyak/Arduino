#pragma once

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_DSI.hpp"

///
/// <summary>
/// LovyanGFX MIPI-DSI panel driver for the HX8394 controller used on the Waveshare
/// ESP32-P4-WIFI6-Touch-LCD-5 (720x1280 portrait 2-lane DSI panel).
/// </summary>
///
struct Panel_HX8394_Waveshare_50 : public lgfx::Panel_DSI
{
protected:
   const uint8_t* getInitParams(size_t listno) const override
   {
      // Send SLPOUT before vendor init commands (matching esp_lcd_hx8394).
      static constexpr uint8_t list0[] =
      {
         1, CMD_SLPOUT,
         0,
      };

      // Set MADCTL, COLMOD, 2-lane DSI config, and vendor init sequence up to second SLPOUT.
      static constexpr uint8_t list1[] =
      {
         2, CMD_MADCTL, 0x00,
         2, CMD_COLMOD, 0x55,
         2, 0xBA, 0x61,
         4, 0xB9, 0xFF, 0x83, 0x94,
         11, 0xB1, 0x48, 0x0A, 0x6A, 0x09, 0x33, 0x54, 0x71, 0x71, 0x2E, 0x45,
         7, 0xBA, 0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0,
         7, 0xB2, 0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F,
         22, 0xB4, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86, 0x75, 0x00, 0x3F, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86,
         34, 0xD3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x32, 0x10, 0x05, 0x00, 0x05, 0x32, 0x13, 0xC1, 0x00, 0x01, 0x32, 0x10, 0x08, 0x00, 0x00, 0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05, 0x37, 0x0C, 0x40,
         45, 0xD5, 0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20, 0x21, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19,
         45, 0xD6, 0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23, 0x22, 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18,
         59, 0xE0, 0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16, 0x13, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x76, 0x80, 0x97, 0xA5, 0xA8, 0xB5, 0xC6, 0x62, 0x63, 0x68, 0x6F, 0x72, 0x78, 0x7F, 0x7F, 0x00, 0x02, 0x08, 0x0D, 0x0C, 0x0E, 0x0F, 0x10, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x78, 0x82, 0x99, 0xA4, 0xA0, 0xB1, 0xC0, 0x5E, 0x5E, 0x64, 0x6B, 0x6C, 0x73, 0x7F, 0x7F,
         2, 0xCC, 0x0B,
         3, 0xC0, 0x1F, 0x73,
         3, 0xB6, 0x6B, 0x6B,
         2, 0xD4, 0x02,
         2, 0xBD, 0x01,
         2, 0xB1, 0x00,
         2, 0xBD, 0x00,
         8, 0xBF, 0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01,
         2, CMD_COLMOD, 0x50,
         1, CMD_SLPOUT,
         0,
      };

      // Display ON after 200 ms SLPOUT delay.
      static constexpr uint8_t list2[] =
      {
         13, 0xB2, 0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x18,
         1, CMD_DISPON,
         0,
      };

      switch (listno)
      {
      case 0:
         return list0;
      case 1:
         return list1;
      case 2:
         return list2;
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
      case 1:
         return 200;
      case 2:
         return 80;
      default:
         return 0;
      }
   }

   bool init_panel()
   {
      auto bus = getBusDSI();
      if (bus == nullptr)
      {
         return false;
      }

      const uint8_t* params;
      for (size_t i = 0; nullptr != (params = getInitParams(i)); i++)
      {
         size_t len;
         while (0 != (len = params[0]))
         {
            bus->writeParams(params[1], &params[2], len - 1);
            params += len + 1;
         }
         vTaskDelay(pdMS_TO_TICKS(getInitDelay(i)));
      }

      return (ESP_OK == esp_lcd_panel_init(_disp_panel_handle));
   }

public:
   bool init(bool use_reset) override
   {
      if (_lines_buffer != nullptr)
      {
         return false;
      }

      // Enable backlight boost converter (GPIO33 = BL_EN) if populated on the board.
      lgfx::gpio_hi(33);
      lgfx::pinMode(33, lgfx::pin_mode_t::output);

      // Note: Do not pass use_reset=true to Panel_FrameBufferBase::init() because on the
      // Waveshare ESP32-P4-WIFI6-Touch-LCD-5 board, GPIO27 drives an NPN inverter (Q10) or
      // active-high reset logic (reset_active_high = 1 in the official Waveshare BSP).
      // Panel_Device::init() assumes active-low reset and leaves pin_rst HIGH after init,
      // which holds the HX8394 in reset!
      if (!Panel_FrameBufferBase::init(false))
      {
         return false;
      }

      if (use_reset && _cfg.pin_rst >= 0)
      {
         lgfx::pinMode(_cfg.pin_rst, lgfx::pin_mode_t::output);
         lgfx::gpio_hi(_cfg.pin_rst);
         vTaskDelay(pdMS_TO_TICKS(10));
         lgfx::gpio_lo(_cfg.pin_rst);
         vTaskDelay(pdMS_TO_TICKS(10));
      }

      auto bus = getBusDSI();
      if (bus == nullptr)
      {
         return false;
      }

      if (init_dpi(bus) && init_panel())
      {
         esp_lcd_dpi_panel_get_frame_buffer(_disp_panel_handle, 1, &(_config_detail.buffer));
      }

      auto ptr = (uint8_t*)_config_detail.buffer;
      if (ptr == nullptr)
      {
         return false;
      }

      const auto height = _cfg.panel_height;
      size_t la_size = height * sizeof(void*);
      uint8_t** lineArray = (uint8_t**)lgfx::heap_alloc_dma(la_size);
      if (nullptr == lineArray)
      {
         return false;
      }
      memset(lineArray, 0, la_size);

      const size_t line_length = ((_cfg.panel_width * _write_bits >> 3) + 3) & ~3;
      _lines_buffer = lineArray;

      for (int y = 0; y < height; y++)
      {
         lineArray[y] = ptr;
         ptr = ptr + line_length;
      }

      return true;
   }
};

///
/// <summary>
/// LovyanGFX display configuration for the Waveshare ESP32-P4-WIFI6-Touch-LCD-5 board.
/// Hardware: 720x1280 HX8394 MIPI-DSI (2-lane) IPS display with GT911 I2C capacitive touch
/// controller and active-high PWM backlight on GPIO26.
/// </summary>
///
class LGFX : public lgfx::LGFX_Device
{
private:
   static constexpr uint8_t I2C_PORT = 0;
   static constexpr uint32_t I2C_FREQ = 400000;
   static constexpr int8_t PIN_SDA = 7;
   static constexpr int8_t PIN_SCL = 8;
   static constexpr int8_t PIN_TOUCH_RST = -1;
   static constexpr int8_t PIN_LCD_BL = 26;
   static constexpr int8_t PIN_LCD_RST = 27;

   lgfx::Bus_DSI _bus_instance;
   Panel_HX8394_Waveshare_50 _panel_instance;
   lgfx::Light_PWM _light_instance;
   lgfx::Touch_GT911 _touch_instance;

public:
   ///
   /// <summary>
   /// Initializes MIPI-DSI bus, HX8394 panel, GT911 touch controller, and backlight configuration.
   /// </summary>
   ///
   LGFX()
   {
      // Configure 2-lane MIPI-DSI bus (700 Mbps per lane, 2.5V PHY powered from LDO channel 3).
      {
         auto cfg = _bus_instance.config();
         cfg.bus_id = 0;
         cfg.lane_num = 2;
         cfg.lane_mbps = 700;
         cfg.ldo_chan_id = 3;
         cfg.ldo_voltage_mv = 2500;
         cfg.lcd_cmd_bits = 8;
         cfg.lcd_param_bits = 8;
         _bus_instance.config(cfg);
         _panel_instance.setBus(&_bus_instance);
      }

      // Configure HX8394 panel (720x1280 portrait native).
      {
         auto cfg = _panel_instance.config();
         cfg.pin_cs = -1;
         cfg.pin_rst = PIN_LCD_RST;
         cfg.pin_busy = -1;
         cfg.memory_width = 720;
         cfg.memory_height = 1280;
         cfg.panel_width = 720;
         cfg.panel_height = 1280;
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

      // Configure MIPI-DSI DPI timing (58 MHz pixel clock).
      {
         auto det = _panel_instance.config_detail();
         det.dpi_freq_mhz = 58;
         det.hsync_back_porch = 20;
         det.hsync_pulse_width = 20;
         det.hsync_front_porch = 40;
         det.vsync_back_porch = 10;
         det.vsync_pulse_width = 4;
         det.vsync_front_porch = 24;
         _panel_instance.config_detail(det);
      }

      // Configure GT911 capacitive touch controller over I2C.
      {
         auto cfg = _touch_instance.config();
         cfg.x_min = 0;
         cfg.x_max = 719;
         cfg.y_min = 0;
         cfg.y_max = 1279;
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

      // Configure backlight (active-high PWM on GPIO26 at 5 kHz, matching the official BSP).
      {
         auto light_cfg = _light_instance.config();
         light_cfg.pin_bl = PIN_LCD_BL;
         light_cfg.invert = false;
         light_cfg.freq = 5000;
         light_cfg.pwm_channel = 7;
         _light_instance.config(light_cfg);
         _panel_instance.setLight(&_light_instance);
      }

      setPanel(&_panel_instance);
   }
};
