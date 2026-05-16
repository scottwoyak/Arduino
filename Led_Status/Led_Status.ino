#include <Status.h>

constexpr auto BLUE_LED_PIN = 2;
constexpr auto GREEN_LED_PIN = 3;
constexpr auto RED_LED_PIN = 4;
constexpr auto WHITE_LED_PIN = 5;
constexpr auto NEO_LED_PIN = 21;

LedStatus status(WHITE_LED_PIN, BLUE_LED_PIN, GREEN_LED_PIN);
//NeoLedStatus status(NEO_LED_PIN);

void setup()
{
   status.begin();
}

void loop()
{
   status.setStatus(Status::NONE);
   delay(1200);

   status.setStatus(Status::WIFI_CONNECTING);
   delay(2200);

   status.setStatus(Status::WEB_CONNECTING);
   delay(2000);

   status.setStatus(Status::READY);
   delay(5000);
}
