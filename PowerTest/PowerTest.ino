
#include <Util.h>
#include <Stopwatch.h>

#include <Adafruit_LPS35HW.h>

Adafruit_LPS35HW lps3x;

void setup() {
   Serial.begin(115200);

   analogReadResolution(12);
   digitalWrite(LED_BUILTIN, LOW);

   lps3x.begin_I2C();
}

double value = 0;
long count = 0;
Stopwatch sw;

void loop() {
   float pressure = 100 * lps3x.readPressure() / 3386.39;
   Serial.println(pressure);
   float temp = 32 + (9.0 / 5) * lps3x.readTemperature();
   Serial.println(temp);
   delay(1000);
   /*
   // 0.01425 - 0.0149
   value += Util::readVolts(PIN_A7);
   count++;

   if (sw.elapsedMillis() > 1000) {
      Serial.print("count: ");
      Serial.print(count);
      Serial.print(" ");
      Serial.print(value / count, 5);
      Serial.println();
      sw.reset();
   }
*/
// 0.0146 - 0.0151
//delay(10);
}
