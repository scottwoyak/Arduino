#pragma once

class Util
{

   /*
   const char* WiFiStatus2Str(uint8_t status) {
      switch (status) {
      case WL_NO_SHIELD: return "WL_NO_SHIELD";
      case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
      case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
      case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
      case WL_CONNECTED: return "WL_CONNECTED";
      case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
      case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
      case WL_DISCONNECTED: return "WL_DISCONNECTED";
      default: return "UNKNOWN";
      }
   }

   bool connectToWifi(const char* ssid, const char* pass, ulong timeoutMs = 0) {
      Serial.print("Connecting to ");
      Serial.print(ssid);

      WiFi.setPins(8, 7, 4, 2);
      int status = WiFi.begin(ssid, pass);

      Stopwatch sw;
      while (status != WL_CONNECTED) {

         if (timeoutMs > 0 && sw.elapsedMillis() > timeoutMs) {
            break;
         }

         // wait for connection:
         Serial.print(".");
         delay(500);
      }
      Serial.println();

      if (status == WL_CONNECTED) {
         Serial.print("Connected to ");
         Serial.println(ssid);
         return true;
      }
      else {
         Serial.println("Connection failed");
         return false;
      }
   }
   */

public:
   static float readVolts(uint8_t pin, uint16_t resolution = 4096, float voltageDivider = 2)
   {
      float volts = analogRead(pin);
      volts *= voltageDivider;    // compensate for voltage divider
      volts *= 3.3;  // Multiply by 3.3V, our reference voltage
      volts /= resolution; // convert to voltage
      return volts;
   }

   static float voltsToPercent(float volts, float fullChargeVolts = 4.1)
   {
      const float MIN_VOLTS = 3.3;
      float percent = 100 * (volts - MIN_VOLTS) / (fullChargeVolts - MIN_VOLTS);
      percent = constrain(percent, 0, 100);
      return percent;
   }

   /// <summary>
   /// Calculates elapsed time between two clock readings, handling wraparound.
   /// Uses unsigned arithmetic: if end &lt; start (clock wrapped), the result
   /// is still correct due to modulo 2^32 arithmetic.
   /// </summary>
   /// <param name="start">Start timestamp from micros() or millis()</param>
   /// <param name="end">End timestamp from micros() or millis()</param>
   /// <returns>Elapsed time in the same units as the input timestamps</returns>
   static unsigned long getSpan(unsigned long start, unsigned long end)
   {
      return end - start;
   }

	static unsigned long microsSince(unsigned long start)
	{
	  return getSpan(start, micros());
	}

	static void reset(float delaySecs=0)
   {
      delay(1000 * delaySecs);

      // TODO this is for the ESP32 which complains about the standard resetFunc() method of reset. We'll
      // need other techniques for different boards.
      ESP.restart();
   }
};