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
            Serial.printf("  spi_host:          %d\n", b_cfg.spi_host);
            Serial.printf("  pin_sclk:          %d\n", b_cfg.pin_sclk);
            Serial.printf("  pin_mosi:          %d\n", b_cfg.pin_mosi);
            Serial.printf("  pin_miso:          %d\n", b_cfg.pin_miso);
            Serial.printf("  pin_dc:            %d\n", b_cfg.pin_dc);
            Serial.printf("  Write Freq:        %d MHz\n", (int) (b_cfg.freq_write / 1000000));
            Serial.printf("  Read Freq:         %d MHz\n", (int) (b_cfg.freq_read / 1000000));
         }

         // 2. Fetch the structural panel configuration copy
         auto p_cfg = panel->config();

         Serial.println("[Panel Dimensions & Memory]");
         Serial.printf("  panel_width:       %d pixels\n", p_cfg.panel_width);
         Serial.printf("  panel_height:      %d pixels\n", p_cfg.panel_height);
         Serial.printf("  memory_width:      %d pixels\n", p_cfg.memory_width);
         Serial.printf("  memory_height:     %d pixels\n", p_cfg.memory_height);

         Serial.println("[Panel Offsets]");
         Serial.printf("  offset_X:          %d\n", p_cfg.offset_x);
         Serial.printf("  offset_Y:          %d\n", p_cfg.offset_y);
         Serial.printf("  offset_rotation:   %d\n", p_cfg.offset_rotation);

         Serial.println("[Hardware Pins]");
         Serial.printf("  pin_cs:            %d\n", p_cfg.pin_cs);
         Serial.printf("  pin_rst:           %d\n", p_cfg.pin_rst);
         Serial.printf("  pin_busy:          %d\n", p_cfg.pin_busy);

         Serial.println("[Signal & Color Settings]");
         Serial.printf("  invert:            %s\n", p_cfg.invert ? "true" : "false");
         Serial.printf("  rgb_order:         %s\n", p_cfg.rgb_order ? "true" : "false");
         Serial.printf("  bus_shared:        %s\n", p_cfg.bus_shared ? "true" : "false");

         Serial.println("[Timing Variables]");
         Serial.printf("  readable:          %s\n", p_cfg.readable ? "true" : "false");
         Serial.printf("  dummy_read_pixel:  %d\n", p_cfg.dummy_read_pixel);
         Serial.printf("  dummy_read_bits:   %d\n", p_cfg.dummy_read_bits);
         Serial.printf("  dlen_16bit:        %s\n", p_cfg.dlen_16bit ? "true" : "false");

         auto light = (lgfx::Light_PWM*)panel->getLight();
         if (light != nullptr)
         {
            auto l_cfg = light->config();
            Serial.println("[Backlight Config]");
            Serial.printf("  pin_bl:            %d\n", l_cfg.pin_bl);
            Serial.printf("  freq:              %d Hz\n", l_cfg.freq);
            Serial.printf("  pwm_channel:       %d\n", l_cfg.pwm_channel);
            Serial.printf("  invert:            %s\n", l_cfg.invert ? "true" : "false");
         }
      }
      else
      {
         Serial.println("Error: Failed to fetch active Panel Instance.");
      }
   }

};
