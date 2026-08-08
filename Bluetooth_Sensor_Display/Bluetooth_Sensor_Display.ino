//
// Measures temperature at 10Hz using an auto-detected TempSensor, maintains a
// 10-value rolling average, and broadcasts the averaged temperature over BLE
// as a GATT server. Bluetooth_Viewer_Display connects to this device and shows
// the temperature.
//
// Runs on ESP32-S3.
//

#include <Arduino.h>
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. arduino ESP32-S3 or arduino M0)."
#endif

#include <BLEDevice.h>
#include <BLEServer.h>

#include "SerialX.h"
#include "Timer.h"
#include "RollingAverage.h"
#include "TempSensor.h"
#include "Feather_ESP32_S3.h"

constexpr auto DEVICE_NAME = "Bluetooth_Sensor_Display";
constexpr auto SERVICE_UUID = "d3c00001-0f92-4b1a-8f3e-9a6b3f5c1a01";
constexpr auto TEMP_CHARACTERISTIC_UUID = "d3c00002-0f92-4b1a-8f3e-9a6b3f5c1a01";
constexpr float SAMPLE_RATE_HZ = 10.0f;
constexpr uint8_t ROLLING_SAMPLES = 10;

Feather_ESP32_S3 arduino;

Format tempFormat("###.# F");

TempSensor tempSensor;
RollingAverage rollingTemp(ROLLING_SAMPLES);
RateTimer sampleTimer(SAMPLE_RATE_HZ);

BLECharacteristic* tempCharacteristic = nullptr;

volatile bool connected = false;

int16_t valueY = 0;

///
/// <summary>
/// Callback tracking server connect/disconnect events.
/// </summary>
///
class _ServerCallback : public BLEServerCallbacks
{
public:
   void onConnect(BLEServer* server) override
   {
      connected = true;
   }

   void onDisconnect(BLEServer* server) override
   {
      connected = false;
      BLEDevice::getAdvertising()->start();
   }
};

///
/// <summary>
/// Starts BLE advertising and creates the temperature GATT service.
/// </summary>
///
void _startBle()
{
   BLEDevice::init(DEVICE_NAME);

   BLEServer* server = BLEDevice::createServer();
   server->setCallbacks(new _ServerCallback());
   BLEService* service = server->createService(SERVICE_UUID);

   tempCharacteristic = service->createCharacteristic(
      TEMP_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

   service->start();

   BLEAdvertising* advertising = BLEDevice::getAdvertising();
   advertising->addServiceUUID(SERVICE_UUID);
   advertising->setScanResponse(true);
   BLEDevice::startAdvertising();
}

void setup()
{
   SerialX::begin();

   Wire.begin();
   arduino.begin();

   if (!tempSensor.begin(true, true))
   {
      Serial.println("Temperature sensor not found");
   }

   _startBle();

   Serial.println("Bluetooth_Sensor_Display advertising...");

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
   if (sampleTimer.ready())
   {
      float tempF = tempSensor.readTemperatureF();
      rollingTemp.set(tempF);

      float avgTempF = rollingTemp.average();
      if (isfinite(avgTempF))
      {
         tempCharacteristic->setValue(avgTempF);
         tempCharacteristic->notify();
      }

      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Bluetooth Sensor", Color::HEADING);

      arduino.setTextSize(5);
      arduino.setCursorY(valueY);
      arduino.printlnC(avgTempF, tempFormat, Color::VALUE);

      arduino.setTextSize(2);
      arduino.setCursor(0, -arduino.charH());
      arduino.println(connected ? "Bluetooth: Connected" : "Bluetooth: Scanning ", connected ? Color::BLUE : Color::GRAY);
   }
}
