#pragma once

class LGFXUtil
{
public:
   static void printLGFX(LGFX* display)
   {
      auto panel = display->getPanel();
      if (panel != nullptr)
      {
         auto bus = (lgfx::Bus_SPI*)panel->getBus();
         if (bus != nullptr)
         {
            auto b_cfg = bus->config();
            Serial.println("[SPI Bus Config]");
            Serial.print("  spi_host:          "); Serial.println(b_cfg.spi_host);
            Serial.print("  pin_sclk:          "); Serial.println(b_cfg.pin_sclk);
            Serial.print("  pin_mosi:          "); Serial.println(b_cfg.pin_mosi);
            Serial.print("  pin_miso:          "); Serial.println(b_cfg.pin_miso);
            Serial.print("  pin_dc:            "); Serial.println(b_cfg.pin_dc);
            Serial.print("  Write Freq:        "); Serial.print(b_cfg.freq_write / 1000000); Serial.println(" MHz");
            Serial.print("  Read Freq:         "); Serial.print(b_cfg.freq_read / 1000000); Serial.println(" MHz");
         }

         // 2. Fetch the structural panel configuration copy
         auto p_cfg = panel->config();

         Serial.println("[Panel Dimensions & Memory]");
         Serial.print("  panel_width:       "); Serial.print(p_cfg.panel_width); Serial.println(" pixels");
         Serial.print("  panel_height:      "); Serial.print(p_cfg.panel_height); Serial.println(" pixels");
         Serial.print("  memory_width:      "); Serial.print(p_cfg.memory_width); Serial.println(" pixels");
         Serial.print("  memory_height:     "); Serial.print(p_cfg.memory_height); Serial.println(" pixels");

         Serial.println("[Panel Offsets]");
         Serial.print("  offset_X:          "); Serial.println(p_cfg.offset_x);
         Serial.print("  offset_Y:          "); Serial.println(p_cfg.offset_y);
         Serial.print("  offset_rotation:   "); Serial.println(p_cfg.offset_rotation);

         Serial.println("[Hardware Pins]");
         Serial.print("  pin_cs:            "); Serial.println(p_cfg.pin_cs);
         Serial.print("  pin_rst:           "); Serial.println(p_cfg.pin_rst);
         Serial.print("  pin_busy:          "); Serial.println(p_cfg.pin_busy);

         Serial.println("[Signal & Color Settings]");
         Serial.print("  invert:            "); Serial.println(p_cfg.invert ? "true" : "false");
         Serial.print("  rgb_order:         "); Serial.println(p_cfg.rgb_order ? "true" : "false");
         Serial.print("  bus_shared:        "); Serial.println(p_cfg.bus_shared ? "true" : "false");

         Serial.println("[Timing Variables]");
         Serial.print("  readable:          "); Serial.println(p_cfg.readable ? "true" : "false");
         Serial.print("  dummy_read_pixel:  "); Serial.println(p_cfg.dummy_read_pixel);
         Serial.print("  dummy_read_bits:   "); Serial.println(p_cfg.dummy_read_bits);
         Serial.print("  dlen_16bit:        "); Serial.println(p_cfg.dlen_16bit ? "true" : "false");

         auto light = (lgfx::Light_PWM*)panel->getLight();
         if (light != nullptr)
         {
            auto l_cfg = light->config();
            Serial.println("[Backlight Config]");
            Serial.print("  pin_bl:            "); Serial.println(l_cfg.pin_bl);
            Serial.print("  freq:              "); Serial.print(l_cfg.freq); Serial.println(" Hz");
            Serial.print("  pwm_channel:       "); Serial.println(l_cfg.pwm_channel);
            Serial.print("  invert:            "); Serial.println(l_cfg.invert ? "true" : "false");
         }
      }
      else
      {
         Serial.println("Error: Failed to fetch active Panel Instance.");
      }
   }

};
