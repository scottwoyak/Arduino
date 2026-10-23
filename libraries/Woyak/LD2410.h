#pragma once

#include <Arduino.h>
#include <array>

#include "Util.h"

///
/// <summary>
/// Driver for the Hi-Link HLK-LD2410C (and LD2410/LD2410B) 24 GHz mmWave human presence
/// radar sensor. Parses continuous binary target-report frames over UART (default
/// 256000 baud, 8N1) and optionally reads the digital OUT presence pin.
/// </summary>
///
class LD2410
{
public:
   static constexpr uint32_t DEFAULT_BAUD_RATE = 256000;
   static constexpr int8_t NO_PIN = -1;

   ///
   /// <summary>
   /// Target presence state reported in each radar data frame.
   /// </summary>
   ///
   enum class TargetState : uint8_t
   {
      NONE = 0x00,
      MOVING = 0x01,
      STATIONARY = 0x02,
      BOTH = 0x03,
   };

private:
   static constexpr uint8_t MIN_PAYLOAD_SIZE = 13;
   static constexpr uint8_t MAX_PAYLOAD_SIZE = 64;
   static constexpr uint8_t FRAME_TYPE_ENGINEERING = 0x01;
   static constexpr uint8_t FRAME_TYPE_BASIC = 0x02;
   static constexpr uint8_t INTRA_FRAME_HEAD = 0xAA;
   static constexpr uint8_t INTRA_FRAME_TAIL = 0x55;
   static constexpr uint8_t INTRA_FRAME_CHECK = 0x00;

   static constexpr uint8_t NUM_HEADER_BYTES = 4;
   static constexpr std::array<uint8_t, NUM_HEADER_BYTES> FRAME_HEADER = { 0xF4, 0xF3, 0xF2, 0xF1 };
   static constexpr std::array<uint8_t, NUM_HEADER_BYTES> FRAME_FOOTER = { 0xF8, 0xF7, 0xF6, 0xF5 };

   enum class ParseState : uint8_t
   {
      HEADER,
      LENGTH_LOW,
      LENGTH_HIGH,
      PAYLOAD,
      FOOTER,
   };

   HardwareSerial* _serial;
   int8_t _rxPin;
   int8_t _txPin;
   int8_t _outPin;

   ParseState _parseState = ParseState::HEADER;
   uint8_t _matchIndex = 0;
   uint16_t _payloadLength = 0;
   uint16_t _payloadIndex = 0;
   std::array<uint8_t, MAX_PAYLOAD_SIZE> _payload{};

   bool _hasFrame = false;
   TargetState _targetState = TargetState::NONE;
   uint16_t _movingDistanceCm = 0;
   uint8_t _movingEnergy = 0;
   uint16_t _stationaryDistanceCm = 0;
   uint8_t _stationaryEnergy = 0;
   uint16_t _detectionDistanceCm = 0;

   ///
   /// <summary>
   /// Resets the UART frame parser to hunt for the next 4-byte frame header.
   /// </summary>
   ///
   void _resetParser()
   {
      _parseState = ParseState::HEADER;
      _matchIndex = 0;
      _payloadLength = 0;
      _payloadIndex = 0;
   }

   ///
   /// <summary>
   /// Decodes a validated radar report payload and updates cached target readings.
   /// </summary>
   /// <returns>True if the payload contained a valid target data frame; false otherwise.</returns>
   ///
   bool _parsePayload()
   {
      if (_payloadLength < MIN_PAYLOAD_SIZE)
      {
         return false;
      }

      uint8_t frameType = _payload[0];
      if ((frameType != FRAME_TYPE_BASIC && frameType != FRAME_TYPE_ENGINEERING) ||
          _payload[1] != INTRA_FRAME_HEAD ||
          _payload[_payloadLength - 2] != INTRA_FRAME_TAIL ||
          _payload[_payloadLength - 1] != INTRA_FRAME_CHECK)
      {
         return false;
      }

      uint8_t rawState = _payload[2] & 0x03;
      _targetState = static_cast<TargetState>(rawState);
      _movingDistanceCm = static_cast<uint16_t>(_payload[3] | (_payload[4] << 8));
      _movingEnergy = _payload[5];
      _stationaryDistanceCm = static_cast<uint16_t>(_payload[6] | (_payload[7] << 8));
      _stationaryEnergy = _payload[8];
      _detectionDistanceCm = static_cast<uint16_t>(_payload[9] | (_payload[10] << 8));
      _hasFrame = true;
      return true;
   }

   ///
   /// <summary>
   /// Feeds a single UART byte into the frame state machine.
   /// </summary>
   /// <param name="byte">Byte read from the sensor UART stream.</param>
   /// <returns>True when a complete, valid target report frame has been decoded.</returns>
   ///
   bool _processByte(uint8_t byte)
   {
      switch (_parseState)
      {
         case ParseState::HEADER:
            if (byte == FRAME_HEADER[_matchIndex])
            {
               _matchIndex++;
               if (_matchIndex == NUM_HEADER_BYTES)
               {
                  _parseState = ParseState::LENGTH_LOW;
               }
            }
            else
            {
               _matchIndex = (byte == FRAME_HEADER[0]) ? 1 : 0;
            }
            return false;

         case ParseState::LENGTH_LOW:
            _payloadLength = byte;
            _parseState = ParseState::LENGTH_HIGH;
            return false;

         case ParseState::LENGTH_HIGH:
            _payloadLength |= static_cast<uint16_t>(byte << 8);
            if (_payloadLength < MIN_PAYLOAD_SIZE || _payloadLength > MAX_PAYLOAD_SIZE)
            {
               _resetParser();
            }
            else
            {
               _payloadIndex = 0;
               _parseState = ParseState::PAYLOAD;
            }
            return false;

         case ParseState::PAYLOAD:
            _payload[_payloadIndex] = byte;
            _payloadIndex++;
            if (_payloadIndex >= _payloadLength)
            {
               _matchIndex = 0;
               _parseState = ParseState::FOOTER;
            }
            return false;

         case ParseState::FOOTER:
            if (byte == FRAME_FOOTER[_matchIndex])
            {
               _matchIndex++;
               if (_matchIndex == NUM_HEADER_BYTES)
               {
                  bool valid = _parsePayload();
                  _resetParser();
                  return valid;
               }
            }
            else
            {
               _resetParser();
            }
            return false;

         default:
            _resetParser();
            return false;
      }
   }

public:
   ///
   /// <summary>
   /// Initializes an LD2410 sensor instance using the given UART port and pins.
   /// </summary>
   /// <param name="rxPin">MCU RX pin connected to the sensor's TX pin.</param>
   /// <param name="txPin">MCU TX pin connected to the sensor's RX pin.</param>
   /// <param name="outPin">MCU digital input pin connected to the sensor's OUT pin, or NO_PIN if unused.</param>
   /// <param name="serial">Hardware UART instance to read frames from (default: &Serial1).</param>
   ///
   LD2410(
      int8_t rxPin = RX,
      int8_t txPin = TX,
      int8_t outPin = NO_PIN,
      HardwareSerial* serial = &Serial1)
      : _serial(serial),
        _rxPin(rxPin),
        _txPin(txPin),
        _outPin(outPin)
   {
   }

   ///
   /// <summary>
   /// Configures the optional digital OUT pin and starts the UART at the sensor's baud rate.
   /// </summary>
   /// <param name="baudRate">UART baud rate (default: 256000).</param>
   ///
   void begin(uint32_t baudRate = DEFAULT_BAUD_RATE)
   {
      ASSERT(_serial != nullptr);

      if (_outPin != NO_PIN)
      {
         pinMode(_outPin, INPUT);
      }

      _serial->begin(baudRate, SERIAL_8N1, _rxPin, _txPin);
   }

   ///
   /// <summary>
   /// Drains available UART bytes and decodes any completed radar frames.
   /// </summary>
   /// <returns>True if at least one new valid frame was decoded during this call.</returns>
   ///
   bool read()
   {
      ASSERT(_serial != nullptr);

      bool gotFrame = false;
      while (_serial->available() > 0)
      {
         uint8_t byte = static_cast<uint8_t>(_serial->read());
         if (_processByte(byte))
         {
            gotFrame = true;
         }
      }
      return gotFrame;
   }

   ///
   /// <summary>
   /// Gets whether at least one valid UART frame has been received from the sensor.
   /// </summary>
   /// <returns>True once a valid frame has been decoded; false before the first frame.</returns>
   ///
   bool hasFrame() const
   {
      return _hasFrame;
   }

   ///
   /// <summary>
   /// Reads the instantaneous digital state of the sensor's OUT pin.
   /// </summary>
   /// <returns>True if the OUT pin is configured and currently HIGH; false otherwise.</returns>
   ///
   bool isOutPinHigh() const
   {
      return (_outPin != NO_PIN) && (digitalRead(_outPin) == HIGH);
   }

   ///
   /// <summary>
   /// Gets whether presence is currently reported via the UART target state.
   /// </summary>
   /// <returns>True if a moving or stationary target is detected; false otherwise.</returns>
   ///
   bool isPresent() const
   {
      return _targetState != TargetState::NONE;
   }

   ///
   /// <summary>
   /// Gets the target presence state from the most recent UART frame.
   /// </summary>
   /// <returns>The latest TargetState value.</returns>
   ///
   TargetState targetState() const
   {
      return _targetState;
   }

   ///
   /// <summary>
   /// Gets a short display label describing the current target state.
   /// </summary>
   /// <returns>"None", "Moving", "Still", or "Both".</returns>
   ///
   const char* targetStateText() const
   {
      switch (_targetState)
      {
         case TargetState::MOVING:
            return "Moving";
         case TargetState::STATIONARY:
            return "Still";
         case TargetState::BOTH:
            return "Both";
         case TargetState::NONE:
         default:
            return "None";
      }
   }

   ///
   /// <summary>
   /// Gets the moving target distance in centimeters from the most recent UART frame.
   /// </summary>
   /// <returns>Moving target distance in cm.</returns>
   ///
   uint16_t movingDistanceCm() const
   {
      return _movingDistanceCm;
   }

   ///
   /// <summary>
   /// Gets the moving target signal energy (0-100) from the most recent UART frame.
   /// </summary>
   /// <returns>Moving target energy percentage (0-100).</returns>
   ///
   uint8_t movingEnergy() const
   {
      return _movingEnergy;
   }

   ///
   /// <summary>
   /// Gets the stationary target distance in centimeters from the most recent UART frame.
   /// </summary>
   /// <returns>Stationary target distance in cm.</returns>
   ///
   uint16_t stationaryDistanceCm() const
   {
      return _stationaryDistanceCm;
   }

   ///
   /// <summary>
   /// Gets the stationary target signal energy (0-100) from the most recent UART frame.
   /// </summary>
   /// <returns>Stationary target energy percentage (0-100).</returns>
   ///
   uint8_t stationaryEnergy() const
   {
      return _stationaryEnergy;
   }

   ///
   /// <summary>
   /// Gets the overall detection distance in centimeters from the most recent UART frame.
   /// </summary>
   /// <returns>Overall detection distance in cm.</returns>
   ///
   uint16_t detectionDistanceCm() const
   {
      return _detectionDistanceCm;
   }
};
