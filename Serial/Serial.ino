
#include "Util.h"

void setup()
{
   Serial.begin(115200);

   // Boards with native USB CDC (e.g. Adafruit Feather ESP32-S3) only make Serial true
   // once a monitor actually opens the port, so waiting with "while (!Serial)" can hang
   // forever if the monitor isn't attached at the right moment. Wait with a timeout
   // instead, so setup() always completes.
   uint32_t waitStart = millis();
   while (!Serial && (millis() - waitStart) < 1000) { delay(10); }

   // Regardless of board/USB type, the first real print after the wait above can still
   // get silently dropped (native USB CDC boards need an initial empty println() to
   // prime the connection; UART-bridge boards need a brief delay while the OS finishes
   // enumerating the port). Both fixes are cheap and harmless on every board, so just
   // always do both rather than trying to detect the exact USB mode.
   delay(1000);
   Serial.println();

   Serial.println("Serial Test Sketch");

   Util::printBoardInfo();
}

uint32_t counter = 0;

void loop()
{
   Serial.print("Test Output: ");
   Serial.println(counter++);
   delay(5000);
}
