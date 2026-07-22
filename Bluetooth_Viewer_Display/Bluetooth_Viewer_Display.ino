//
// Scans for a nearby Bluetooth_Sensor_Display device and, once its BLE signal strength
// (RSSI) crosses a proximity threshold, automatically connects and displays
// the broadcasted temperature. Disconnects and resumes scanning if the
// connection is lost.
//
// Runs on ESP32-S3 with a Feather ESP32-S3 TFT display.
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

#include "SerialX.h"
#include "TimedAverage.h"
#include "Feather_ESP32_S3.h"

constexpr auto DEVICE_NAME = "Bluetooth_Viewer_Display";
constexpr auto SERVICE_UUID = "d3c00001-0f92-4b1a-8f3e-9a6b3f5c1a01";
constexpr auto TEMP_CHARACTERISTIC_UUID = "d3c00002-0f92-4b1a-8f3e-9a6b3f5c1a01";
constexpr int8_t PROXIMITY_RSSI_THRESHOLD = -70;
constexpr uint32_t SCAN_DURATION_SECS = 3;
constexpr uint16_t RSSI_AVERAGE_PERIOD_MS = 1000;

Feather_ESP32_S3 arduino;

Format tempFormat("###.# F");
Format rssiFormat("###", Format::Alignment::RIGHT);

BLEScan* bleScan = nullptr;
BLEClient* bleClient = nullptr;
BLEAdvertisedDevice* foundDevice = nullptr;
BLERemoteCharacteristic* tempCharacteristic = nullptr;

TimedAverage rssiAverage(RSSI_AVERAGE_PERIOD_MS);

volatile bool candidateFound = false;
volatile float latestTempF = NAN;
volatile bool connected = false;

int16_t valueY = 0;

///
/// <summary>
/// Callback invoked for each BLE advertisement seen during a scan.
/// </summary>
///
class _AdvertisedDeviceCallback : public BLEAdvertisedDeviceCallbacks
{
public:
   void onResult(BLEAdvertisedDevice device) override
   {
      if (!device.haveServiceUUID() || !device.isAdvertisingService(BLEUUID(SERVICE_UUID)))
      {
         return;
      }

      if (device.getRSSI() < PROXIMITY_RSSI_THRESHOLD)
      {
         return;
      }

      delete foundDevice;
      foundDevice = new BLEAdvertisedDevice(device);
      candidateFound = true;
   }
};

///
/// <summary>
/// Callback invoked when the temperature characteristic notifies a new value.
/// </summary>
/// <param name="characteristic">Characteristic that changed</param>
/// <param name="data">Raw notification payload</param>
/// <param name="length">Length of the payload in bytes</param>
/// <param name="isNotify">True if this was a notification</param>
///
void _onTempNotify(BLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify)
{
   if (length >= sizeof(float))
   {
      float tempF;
      memcpy(&tempF, data, sizeof(float));
      latestTempF = tempF;
   }
}

///
/// <summary>
/// Callback tracking client connect/disconnect events.
/// </summary>
///
class _ClientCallback : public BLEClientCallbacks
{
public:
   void onConnect(BLEClient* client) override
   {
      connected = true;
   }

   void onDisconnect(BLEClient* client) override
   {
      connected = false;
      latestTempF = NAN;
      rssiAverage.reset();
   }
};

///
/// <summary>
/// Connects to a discovered temperature device and subscribes to notifications.
/// </summary>
/// <returns>True if connection and subscription succeeded.</returns>
///
bool _connectToDevice()
{
   bleClient = BLEDevice::createClient();
   bleClient->setClientCallbacks(new _ClientCallback());

   if (!bleClient->connect(foundDevice))
   {
      Serial.println("Connect failed");
      return false;
   }

   BLERemoteService* service = bleClient->getService(SERVICE_UUID);
   if (service == nullptr)
   {
      Serial.println("Service not found");
      bleClient->disconnect();
      return false;
   }

   tempCharacteristic = service->getCharacteristic(TEMP_CHARACTERISTIC_UUID);
   if (tempCharacteristic == nullptr)
   {
      Serial.println("Characteristic not found");
      bleClient->disconnect();
      return false;
   }

   if (tempCharacteristic->canRead())
   {
      String value = tempCharacteristic->readValue();
      if (value.length() >= sizeof(float))
      {
         float tempF;
         memcpy(&tempF, value.c_str(), sizeof(float));
         latestTempF = tempF;
      }
   }

   if (tempCharacteristic->canNotify())
   {
      tempCharacteristic->registerForNotify(_onTempNotify);
   }

   return true;
}

void setup()
{
   SerialX::begin();

   arduino.begin();

   BLEDevice::init(DEVICE_NAME);
   bleScan = BLEDevice::getScan();
   bleScan->setAdvertisedDeviceCallbacks(new _AdvertisedDeviceCallback());
   bleScan->setActiveScan(true);
   bleScan->start(SCAN_DURATION_SECS, false);

   arduino.setTextSize(3);
   int16_t headerCharH = arduino.charH();

   arduino.setTextSize(5);
   int16_t valueCharH = arduino.charH();

   arduino.setTextSize(2);
   int16_t footerCharH = arduino.charH();

   int16_t footerTopY = arduino.height() - footerCharH;
   valueY = headerCharH + (footerTopY - headerCharH - valueCharH) / 2;
}

void loop()
{
   if (!connected)
   {
      if (candidateFound)
      {
         candidateFound = false;
         bleScan->stop();

         if (_connectToDevice())
         {
            Serial.println("Connected");
         }
         else
         {
            bleScan->start(SCAN_DURATION_SECS, false);
         }
      }
      else
      {
         bleScan->start(SCAN_DURATION_SECS, false);
      }
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Bluetooth Viewer", Color::HEADING);

   arduino.setTextSize(5);
   arduino.setCursorY(valueY);
   if (connected)
   {
      arduino.printlnC(latestTempF, tempFormat, Color::VALUE);
   }
   else
   {
      arduino.printlnC(tempFormat, Color::GRAY);
   }

   arduino.setTextSize(2);
   arduino.setCursor(0, -arduino.charH());
   arduino.println(connected ? "Bluetooth: Connected" : "Bluetooth: Scanning ", connected ? Color::BLUE : Color::GRAY);

   arduino.setCursor(arduino.width(), -arduino.charH());
   if (connected)
   {
      rssiAverage.set((float)bleClient->getRssi());
      arduino.printlnR(rssiAverage.average(), rssiFormat, Color::GRAY);
   }
   else
   {
      arduino.printlnR(rssiFormat, Color::GRAY);
   }
}
