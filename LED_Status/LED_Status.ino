#include "Status.h"

constexpr uint8_t RED_PIN = 1;
constexpr uint8_t GREEN_PIN = 2;
constexpr uint8_t BLUE_PIN = 3;

RGBLEDStatus status(RED_PIN, GREEN_PIN, BLUE_PIN);

void setup()
{
   status.begin();
}

void loop()
{
   status.setStatus(Status::NONE);
   delay(1000);

   status.setStatus(Status::STARTED);
   delay(1000);

   status.setStatus(Status::WIFI_CONNECTING);
   delay(2000);

   status.setStatus(Status::WEB_CONNECTING);
   delay(2000);

   status.setStatus(Status::READY);
   delay(5000);
}
