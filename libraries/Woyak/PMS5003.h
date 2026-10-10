#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <array>

#include "Util.h"

///
/// <summary>
/// Driver for the Plantower PMS5003 particulate matter sensor, read over a UART. The
/// sensor streams a 32 byte frame about once per second; call read() frequently to
/// consume bytes and update the latest values. Concentrations are in ug/m3 and
/// particle counts are per 0.1 L of air.
/// </summary>
///
class PMS5003
{
private:
   static constexpr uint32_t BAUD_RATE = 9600;
   static constexpr uint8_t FRAME_SIZE = 32;
   static constexpr uint8_t START_BYTE_1 = 0x42;
   static constexpr uint8_t START_BYTE_2 = 0x4D;
   static constexpr uint8_t NUM_DATA_WORDS = 13;

   HardwareSerial* _serial = nullptr;
   std::array<uint8_t, FRAME_SIZE> _frame;
   uint8_t _frameLength = 0;
   std::array<uint16_t, NUM_DATA_WORDS> _data = {};
   bool _hasData = false;

   ///
   /// <summary>
   /// Gets one big-endian data word from the most recently validated frame.
   /// </summary>
   /// <param name="index">Zero-based data word index.</param>
   /// <returns>The data word.</returns>
   ///
   uint16_t _word(uint8_t index) const
   {
      return _data[index];
   }

   ///
   /// <summary>
   /// Validates the frame buffer's checksum and, if valid, copies its data words.
   /// </summary>
   /// <returns>True if the frame was valid; otherwise false.</returns>
   ///
   bool _parseFrame()
   {
      uint16_t sum = 0;
      for (uint8_t i = 0; i < FRAME_SIZE - 2; i++)
      {
         sum += _frame[i];
      }

      uint16_t checksum = (_frame[FRAME_SIZE - 2] << 8) | _frame[FRAME_SIZE - 1];
      if (sum != checksum)
      {
         return false;
      }

      // frame: 2 start bytes, 2 length bytes, then the data words
      for (uint8_t i = 0; i < NUM_DATA_WORDS; i++)
      {
         _data[i] = (_frame[4 + 2 * i] << 8) | _frame[5 + 2 * i];
      }
      return true;
   }

public:
   ///
   /// <summary>
   /// Starts the UART connection to the sensor.
   /// </summary>
   /// <param name="serial">The UART connected to the sensor's TX pin.</param>
   /// <param name="rxPin">The pin connected to the sensor's TX pin.</param>
   /// <param name="txPin">The pin connected to the sensor's RX pin.</param>
   /// <returns>Always true; the sensor can only be detected once its first frame arrives (see hasData()).</returns>
   ///
   bool begin(HardwareSerial* serial, int8_t rxPin, int8_t txPin)
   {
      ASSERT(serial != nullptr);

      _serial = serial;
      _serial->begin(BAUD_RATE, SERIAL_8N1, rxPin, txPin);
      return true;
   }

   ///
   /// <summary>
   /// Consumes any available serial bytes and updates the values when a complete, valid
   /// frame has been received. Call frequently from loop().
   /// </summary>
   /// <returns>True if a new valid frame was received; otherwise false.</returns>
   ///
   bool read()
   {
      ASSERT(_serial != nullptr);

      bool updated = false;
      while (_serial->available() > 0)
      {
         uint8_t b = _serial->read();

         // resynchronize on the start bytes
         if (_frameLength == 0 && b != START_BYTE_1)
         {
            continue;
         }
         if (_frameLength == 1 && b != START_BYTE_2)
         {
            _frameLength = (b == START_BYTE_1) ? 1 : 0;
            continue;
         }

         _frame[_frameLength++] = b;
         if (_frameLength == FRAME_SIZE)
         {
            _frameLength = 0;
            if (_parseFrame())
            {
               _hasData = true;
               updated = true;
            }
         }
      }
      return updated;
   }

   /// <summary>True once at least one valid frame has been received.</summary>
   bool hasData() const { return _hasData; }

   /// <summary>PM1.0 concentration (standard particle), ug/m3.</summary>
   float pm1() const { return _word(0); }

   /// <summary>PM2.5 concentration (standard particle), ug/m3.</summary>
   float pm25() const { return _word(1); }

   /// <summary>PM10 concentration (standard particle), ug/m3.</summary>
   float pm10() const { return _word(2); }

   /// <summary>PM1.0 concentration (atmospheric environment), ug/m3.</summary>
   float pm1Env() const { return _word(3); }

   /// <summary>PM2.5 concentration (atmospheric environment), ug/m3.</summary>
   float pm25Env() const { return _word(4); }

   /// <summary>PM10 concentration (atmospheric environment), ug/m3.</summary>
   float pm10Env() const { return _word(5); }

   ///
   /// <summary>
   /// Gets a raw data word from the most recent valid frame. Words 6-11 are the particle
   /// counts per 0.1 L (&gt;0.3, 0.5, 1.0, 2.5, 5.0 and 10 um); word 12 holds the firmware
   /// version (high byte) and error code (low byte).
   /// </summary>
   /// <param name="index">Zero-based data word index (0-12).</param>
   /// <returns>The raw data word.</returns>
   ///
   uint16_t rawWord(uint8_t index) const
   {
      ASSERT(index < NUM_DATA_WORDS);

      return _word(index);
   }
};
