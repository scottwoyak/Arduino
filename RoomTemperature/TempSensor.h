
#include <Adafruit_BME280.h>
#include <Adafruit_SHT31.h>

class ITempSensor {
public:
   virtual bool begin() = 0;
   virtual float readTemperature() = 0;
   virtual float readHumidity() = 0;
};

class NullSensor : public ITempSensor {
   virtual bool begin() {
      return true;
   }

   virtual float readTemperature() {
      return NAN;
   }

   virtual float readHumidity() {
      return NAN;
   }
};

class BME280Sensor : public ITempSensor {
private:
   Adafruit_BME280 bme;

public:
   virtual bool begin() {
      return bme.begin();
   }
   virtual float readTemperature() {
      return bme.readTemperature();
   }

   virtual float readHumidity() {
      return bme.readHumidity();
   }
};

class SHT31Sensor : public ITempSensor {
private:
   Adafruit_SHT31 sht;

public:
   virtual bool begin() {
      return sht.begin();
   }
   virtual float readTemperature() {
      return sht.readTemperature();
   }

   virtual float readHumidity() {
      return sht.readHumidity();
   }
};

class TempSensorFactory {
public:
   static ITempSensor* create() {
      if (I2C::exists(BME280_ADDRESS)) {
         Serial.println("BME280 Sensor Detected");
         return new BME280Sensor();
      }
      else if (I2C::exists(SHT31_DEFAULT_ADDR)) {
         Serial.println("SHT31 Sensor Detected");
         return new SHT31Sensor();
      }
      else {
         Serial.println("No Temperature Sensor Found");
         return new NullSensor();
      }
   }
};

