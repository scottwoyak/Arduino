
/*
#include "OLEDVL53L0XDistanceSketch.h""

OLEDVL53L0XDistanceSketch sketch;

void setup() {
   sketch.setup();
}

void loop() {
   sketch.loop();
}
*/


#include "WiFiSettings.h"
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <Stopwatch.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

class AdafruitIO_WiFiEx : public AdafruitIO_WiFi {
public:
   AdafruitIO_WiFiEx(
      const char* user,
      const char* key,
      const char* ssid,
      const char* pass
   ) : AdafruitIO_WiFi(user, key, ssid, pass)
   {
   }

   HttpClient* getHttp() {
      return this->_http;
   }
};

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFiEx io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed = io.feed("cabin.temperature");

void setup() {
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   Serial.println("Starting ");

   // connect to the wireless
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);

   // connect to io.adafruit.com
   Util::connectToAdafruitIO(&io);

   // we are connected
   Serial.println(io.statusText());

   WiFi.maxLowPowerMode();

   Stopwatch sw;
   getValues();
   Serial.print(sw.elapsedSecs());
   Serial.println(" s");
}

void getValues() {

   String url = "/api/v2/scottwoyak/feeds/cabin.temperature/data?limit=20&include=value";

   HttpClient* http = io.getHttp();
   http->beginRequest();
   http->get(url.c_str());
   http->sendHeader("X-AIO-Key", IO_KEY);
   http->endRequest();

   int status = http->responseStatusCode();
   String body = http->responseBody();

   if (status >= 200 && status <= 299) {

      Serial.println(body);
   }
   else {
      Serial.print("Could not get value. status: ");
      Serial.print(status);
      Serial.println();

      Serial.print("Response Body: ");
      Serial.print(http->responseBody());
   }
}

void loop() {

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Serial.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   //getValues();

   /*
   AdafruitIO_Data* data = temperatureFeed->lastValue();
   float f = data->toFloat();
   Serial.println("----");
   Serial.println(f);
   Serial.println("<<<<<<");
   Serial.println(data->value());

   // 15 extra for api path, 12 for /data/retain, 1 for null
   String url = "/api/v2/scottwoyak/feeds/cabin.temperature/data/retain";
*/


/*
WiFiSSLClient sslClient;
HttpClient client(sslClient, "io.adafruit.com", 443);
client.connect()


io._http->beginRequest();
io->_http->get(url.c_str());
io->_http->sendHeader("X-AIO-Key", _io->_key);
io->_http->endRequest();

int status = _io->_http->responseStatusCode();
String body = _io->_http->responseBody();

if (status >= 200 && status <= 299) {

   if (body.length() > 0) {
      return new AdafruitIO_Data(this, body.c_str());
   }

   return NULL;

}
else {

   AIO_ERROR_PRINT("error retrieving lastValue, status: ");
   AIO_ERROR_PRINTLN(status);
   AIO_ERROR_PRINT("response body: ");
   AIO_ERROR_PRINTLN(_io->_http->responseBody());

   return NULL;
}
}
*/
}

