#pragma once

class CapacitorSensor
{
private:
   // State Management Flags
   enum State
   {
      IDLE,
      DISCHARGING,
      CHARGING,
      COMPLETE
   };

   uint8_t _chargePin;
   uint8_t _sensePin;

   // Timing Registers (volatile because they are modified inside the ISR)
   volatile State _state = State::IDLE;
   volatile uint64_t _startTime = 0;
   volatile uint64_t _endTime = 0;
   volatile uint64_t _chargeTimeUs = 0;

   static inline CapacitorSensor* _instance;

   static void _onPinRising()
   {
      CapacitorSensor::_instance->_onCharged();
   }

   void _onCharged()
   {
      if (_state == CHARGING)
      {
         _endTime = esp_timer_get_time(); // Read 64-bit microsecond hardware clock
         _chargeTimeUs = _endTime - _startTime;
         _state = COMPLETE;
      }
   }

public:
   CapacitorSensor(uint8_t chargePin, uint8_t sensePin)
   {
      _chargePin = chargePin;
      _sensePin = sensePin;
      _instance = this;
   }

   uint32_t chargeTimeUs()
   {
      return (uint32_t)_chargeTimeUs;
   }

   void begin()
   {
      pinMode(_chargePin, OUTPUT);
      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, INPUT);
   }

   bool loop()
   {
      bool measurementTaken = false;

      // FSM (Finite State Machine) Engine for Capacitance
      switch (_state)
      {
      case IDLE:
         // Step A: Disable interrupt so discharging doesn't trigger false flags
         detachInterrupt(digitalPinToInterrupt(_sensePin));

         // Discharge the aluminum bars completely to 0V
         digitalWrite(_chargePin, LOW);
         pinMode(_sensePin, OUTPUT);
         digitalWrite(_sensePin, LOW);

         _startTime = esp_timer_get_time();
         _state = DISCHARGING;
         break;

      case DISCHARGING:
         // Give the bars 500 microseconds to completely empty their electrical charge
         if (esp_timer_get_time() - _startTime > 5000)
         {
            // Step B: CRITICAL ORDER OF OPERATIONS
            // 1. Send electrical wave down the resistor FIRST
            digitalWrite(_chargePin, HIGH);

            // 2. Reset start time benchmark immediately
            _startTime = esp_timer_get_time();
            _state = CHARGING;

            // 3. Set pin back to input and attach the interrupt cleanly
            pinMode(_sensePin, INPUT);
            attachInterrupt(digitalPinToInterrupt(_sensePin), _onPinRising, RISING);
         }
         break;

      case CHARGING:
         // Safety Timeout: If water is deep or shorted, don't let it hang forever
         if (esp_timer_get_time() - _startTime > 200000) // 200ms threshold max
         {
            detachInterrupt(digitalPinToInterrupt(_sensePin));
            _state = IDLE; // Reset if timeout occurs
         }
         break;

      case COMPLETE:
         // Step C: Processing and Math 
         detachInterrupt(digitalPinToInterrupt(_sensePin)); // Clean up interrupt state

         // Ready for next sequence loop
         _state = IDLE;
         measurementTaken = true;
      }

      return measurementTaken;
   }
};
