// The WaveShare ESP32-S3-Zero has no built-in LED, so this uses the sensor board's
// general-purpose LED pin (see WaveShare_ESP32_S3_Zero_Sensors::DEFAULT_LED_PIN)
// instead of BUILTIN_LED.
#include "LED.h"
#include "Waveshare_ESP32_S3_Zero.h"

#ifdef ARDUINO_WAVESHARE_ESP32_S3_ZERO
constexpr uint8_t LED_PIN = WaveShare_ESP32_S3_Zero_Sensors::DEFAULT_LED_PIN;
#else
constexpr uint8_t LED_PIN = BUILTIN_LED;
#endif

LED led(LED_PIN);

void setup()
{
   led.begin();
   led.blink(200);
}

void loop()
{
}
