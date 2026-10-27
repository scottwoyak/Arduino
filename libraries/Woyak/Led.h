#pragma once

#include <Arduino.h>
#include <functional>

#include "ArduinoBoardId.h"
#include "ColorX.h"
#include "Util.h"

#ifdef ARDUINO_NEOPIXEL_SUPPORTED
#include <Adafruit_NeoPixel.h>
#endif

///
/// <summary>
/// Common LED colors/lens tints, used to look up a default calibration factor via
/// ledColorCalibrationFactor(). Values are rough starting points based on typical LED
/// die efficiency and human photopic sensitivity (green/yellow need the least boost,
/// red/blue need the most), not measurements of any specific part - individual LEDs
/// still vary by manufacturer/bin, so treat these as a starting point to fine-tune with
/// LED_Calibrator_Playground rather than a precise/authoritative value.
/// </summary>
///
enum class LEDColor
{
   UNKNOWN,
   RED,
   BLUE,
   GREEN,
   YELLOW,
   ORANGE,
   CLEAR_WHITE,
   CLEAR_PINK,
   CLEAR_RED,
   CLEAR_GREEN,
   CLEAR_BLUE,
   CLEAR_PURPLE,
};

///
/// <summary>
/// Gets the default calibration factor for a common LED color/lens tint (see LEDColor).
/// </summary>
/// <param name="color">The LED color/lens tint to look up.</param>
/// <returns>A default calibration factor from 0.0 to 1.0.</returns>
///
inline float ledColorCalibrationFactor(LEDColor color)
{
   switch (color)
   {
      case LEDColor::UNKNOWN: return 1.0f;
      case LEDColor::RED: return 0.65f;
      case LEDColor::BLUE: return 1.0f;
      case LEDColor::GREEN: return 0.35f;
      case LEDColor::YELLOW: return 0.35f;
      case LEDColor::ORANGE: return 0.5f;
      case LEDColor::CLEAR_WHITE: return 1.0f;
      case LEDColor::CLEAR_PINK: return 0.65f;
      case LEDColor::CLEAR_RED: return 0.65f;
      case LEDColor::CLEAR_GREEN: return 0.35f;
      case LEDColor::CLEAR_BLUE: return 1.0f;
      case LEDColor::CLEAR_PURPLE: return 1.0f;
      default: return 1.0f;
   }
}

///
/// <summary>
/// Base class for programmable LEDs with on/off and blinking support.
/// </summary>
/// <remarks>
/// Provides core functionality for controlling LEDs including brightness levels
/// and blinking patterns. Subclasses handle ESP32 timer-based updates or FastLED integration.
/// </remarks>
///
class BasicLED
{
protected:
   uint8_t _pin;
   uint16_t _blinkIntervalMs = 0;
   uint8_t _level = 255;
   float _calibrationFactor = 1.0f;
   bool _isOn = false;
   unsigned long _nextToggleMs = 0;
   bool _flashActive = false;
   unsigned long _flashEnd = 0;
   volatile bool _flashRequested = false;
   volatile uint16_t _flashDurationMs = 0;
   bool _pendingStopBlink = false;
   bool _pendingIsOn = false;
   bool _hasPendingAction = false;
   std::function<void()> _pendingAction;
#ifdef LED_BLINK_DIAGNOSTICS
   unsigned long _lastTransitionMs = 0;
#endif

   ///
   /// <summary>
   /// Applies the current LED state to the GPIO pin.
   /// </summary>
   ///
   virtual void _apply()
   {
      if (_isOn)
      {
         analogWrite(_pin, _level * _calibrationFactor);
      }
      else
      {
         analogWrite(_pin, 0);
      }
   }

public:
   ///
   /// <summary>
   /// Constructs a BasicLED on the specified pin.
   /// </summary>
   /// <param name="pin">GPIO pin number for the LED</param>
   /// <param name="color">LED color/lens tint used to seed a default calibration factor (see ledColorCalibrationFactor()); defaults to LEDColor::UNKNOWN (no scaling).</param>
   ///
   BasicLED(uint8_t pin, LEDColor color = LEDColor::UNKNOWN)
   {
      _pin = pin;
      _calibrationFactor = ledColorCalibrationFactor(color);
   }

   ///
   /// <summary>
   /// Destructor for BasicLED.
   /// </summary>
   ///
   virtual ~BasicLED() {}

   ///
   /// <summary>
   /// Initializes the LED pin for output.
   /// </summary>
   ///
   virtual void begin()
   {
      if (_pin > 0)
      {
         pinMode(_pin, OUTPUT);
         digitalWrite(_pin, LOW);
      }
   }

   ///
   /// <summary>
   /// Turns the LED on at its currently configured brightness level.
   /// </summary>
   ///
   void turnOn()
   {
      turnOn(_level / 255.0f);
   }

   ///
   /// <summary>
   /// Turns the LED on at the given brightness level. If the LED is currently
   /// blinking, the switch to solid on is deferred until the current blink cycle
   /// completes, so the user doesn't see a truncated "half flash".
   /// </summary>
   /// <param name="level">Normalized brightness from 0.0 (off) to 1.0 (full).</param>
   ///
   virtual void turnOn(float level)
   {
      _level = constrain(255 * level, 0, 255);

      if (_blinkIntervalMs > 0)
      {
         _pendingStopBlink = true;
         _pendingIsOn = true;
         return;
      }

      _isOn = true;
      _apply();
   }

   ///
   /// <summary>
   /// Immediately turns the LED on at its configured brightness level, canceling any
   /// current blink cycle or pending deferred action rather than waiting for the next
   /// rising edge. Intended for urgent state changes (e.g. an error indicator) that
   /// must be visible even if the caller can't rely on loop() running again soon
   /// afterward (e.g. right before a blocking delay/reset).
   /// </summary>
   ///
   void turnOnNow()
   {
      _blinkIntervalMs = 0;
      _pendingStopBlink = false;
      _flashActive = false;
      _hasPendingAction = false;
      _pendingAction = nullptr;
      _isOn = true;
      _apply();
   }

   ///
   /// <summary>
   /// Turns the LED off. If the LED is currently blinking, the switch to solid off is
   /// deferred until the current blink cycle completes, so the user doesn't see a
   /// truncated "half flash".
   /// </summary>
   ///
   virtual void turnOff()
   {
      if (_blinkIntervalMs > 0)
      {
         _pendingStopBlink = true;
         _pendingIsOn = false;
         return;
      }

      _isOn = false;
      _apply();
   }

   ///
   /// <summary>
   /// Sets the LED to blink with the specified interval.
   /// </summary>
   /// <param name="blinkIntervalMs">Half-period of the blink cycle in milliseconds</param>
   ///
   void blink(uint16_t blinkIntervalMs)
   {
      // Idempotent: if already blinking at this interval, leave the phase alone so
      // redundant calls (e.g. setStatus() re-applying the same blinking status on
      // every loop() iteration while reconnecting) don't reset the schedule and cut
      // the current on/off phase short.
      if (_blinkIntervalMs == blinkIntervalMs && !_pendingStopBlink)
      {
         return;
      }

      // no idea why this is needed, but without it, all the leds blink
      delayMicroseconds(50);

      _pendingStopBlink = false;
      _blinkIntervalMs = blinkIntervalMs;
      _isOn = true;
      _nextToggleMs = millis() + blinkIntervalMs;
      _apply();
   }

   ///
   /// <summary>
   /// Turns the LED on for the specified duration, then automatically turns it back off.
   /// Non-blocking; the turn-off is handled by loop().
   /// </summary>
   /// <param name="durationMs">How long the LED should stay on, in milliseconds</param>
   ///
   void flash(uint16_t durationMs)
   {
      _blinkIntervalMs = 0; // no blinking while flashing
      _pendingStopBlink = false;
      _isOn = true;
      _apply();

      _flashActive = true;
      _flashEnd = millis() + durationMs;
   }

   ///
   /// <summary>
   /// Requests a non-blocking LED flash from an interrupt service routine (ISR). The
   /// hardware update is deferred to the next loop() iteration on the LED's background
   /// task, since PWM/RMT drivers are not safe to invoke from interrupt context.
   /// </summary>
   /// <param name="durationMs">How long the LED should stay on, in milliseconds</param>
   ///
   void ARDUINO_ISR_ATTR flashFromISR(uint16_t durationMs)
   {
      _flashDurationMs = durationMs;
      _flashRequested = true;
   }

   ///
   /// <summary>
   /// Gets whether the LED is currently on.
   /// </summary>
   /// <returns>True if the LED is currently on; false otherwise.</returns>
   ///
   bool isOn() const
   {
      return _isOn;
   }

   ///
   /// <summary>
   /// Schedules a state-changing action (e.g. setColor()+turnOn()/turnOff()/blink()) to
   /// run at the next off-\>on transition of the current blink cycle, so switching between
   /// two different blinking states (or a blinking state and a solid color) doesn't cut
   /// the current on/off phase short. If the LED isn't currently blinking, the action runs
   /// immediately.
   /// </summary>
   /// <param name="action">The state-changing action to run.</param>
   ///
   void runAtNextRisingEdge(std::function<void()> action)
   {
      if (_blinkIntervalMs == 0)
      {
         action();
         return;
      }

      _pendingAction = action;
      _hasPendingAction = true;
   }

   ///
   /// <summary>
   /// Sets the LED brightness level as an integer (0-255).
   /// </summary>
   /// <param name="level">Brightness level from 0 (off) to 255 (full brightness)</param>
   ///
   virtual void setLevel(uint8_t level)
   {
      _level = constrain(level, 0, 255);
      _apply();
   }

   ///
   /// <summary>
   /// Sets the LED brightness level as a normalized float (0.0-1.0).
   /// </summary>
   /// <param name="level">Normalized brightness from 0.0 (off) to 1.0 (full)</param>
   ///
   virtual void setLevel(float level)
   {
      _level = constrain(255 * level, 0, 255);
      _apply();
   }

   ///
   /// <summary>
   /// Sets a per-LED calibration factor that scales maximum brightness (0.0-1.0). Lets
   /// different LED/resistor combinations be calibrated so the same setLevel() value
   /// produces the same perceived brightness across LEDs; setLevel()'s own 0.0-1.0 range
   /// is unaffected.
   /// </summary>
   /// <param name="factor">Scaling factor from 0.0 (off) to 1.0 (no scaling, full brightness)</param>
   ///
   void setCalibrationFactor(float factor)
   {
      _calibrationFactor = constrain(factor, 0.0f, 1.0f);
      _apply();
   }

   ///
   /// <summary>
   /// Gets the current calibration factor.
   /// </summary>
   /// <returns>The calibration factor, from 0.0 to 1.0</returns>
   ///
   float getCalibrationFactor() const
   {
      return _calibrationFactor;
   }

   ///
   /// <summary>
   /// Updates LED blinking state. Call periodically or override in subclass.
   /// </summary>
   ///
   virtual void loop()
   {
      if (_flashRequested)
      {
         _flashRequested = false;
         flash(_flashDurationMs);
      }

      if (_flashActive)
      {
         if ((long)(millis() - _flashEnd) >= 0)
         {
            _flashActive = false;
            _isOn = false;
            _apply();
         }
      }
      else if (_blinkIntervalMs > 0)
      {
         // Schedule the next toggle relative to the last intended deadline rather than
         // re-deriving phase from a fixed absolute anchor. If a single loop() call is
         // delayed (e.g. by CPU contention from WiFi activity), this produces one
         // slightly elongated phase followed by a normal one, instead of a truncated
         // phase followed by a visibly compressed one.
         if ((long)(millis() - _nextToggleMs) >= 0)
         {
            bool newIsOn = !_isOn;
            _nextToggleMs += _blinkIntervalMs;

#ifdef LED_BLINK_DIAGNOSTICS
            unsigned long now = millis();
            long sinceLastTransition = (long)(now - _lastTransitionMs);
            Serial.printf("[LED %u] transition to %s at %lu, %ld ms since last transition (expected ~%u)\n",
               _pin, newIsOn ? "ON" : "OFF", now, sinceLastTransition, _blinkIntervalMs);
            _lastTransitionMs = now;
#endif

            // A full on+off cycle only completes at the rising edge (off -> on), so
            // that's the only point a pending stop/action can be applied without the
            // user seeing a truncated "half flash".
            if (newIsOn && _hasPendingAction)
            {
               _hasPendingAction = false;
               std::function<void()> action = _pendingAction;
               _pendingAction = nullptr;

               // Let the pending action establish a fresh state (solid or blinking)
               // instead of applying the old cycle's rising edge.
               _blinkIntervalMs = 0;
               _pendingStopBlink = false;
               _isOn = false;
               action();
               return;
            }

            if (_pendingStopBlink && newIsOn)
            {
               _pendingStopBlink = false;
               _blinkIntervalMs = 0;
               _isOn = _pendingIsOn;
            }
            else
            {
               _isOn = newIsOn;
            }
            _apply();
         }
      }
   }
};

///
/// <summary>
/// LED controller using a dedicated ESP32 FreeRTOS task for automatic updates.
/// </summary>
/// <remarks>
/// Runs its own background task (rather than a software timer callback) to manage
/// blinking patterns. A shared FreeRTOS software timer's callback runs on the single,
/// system-wide Timer Service ("Tmr Svc") task, and per FreeRTOS's own rules that task's
/// callbacks must never block - but NeoPixelLED's _apply() calls into Adafruit_NeoPixel's
/// RMT-based show(), which blocks the calling task until transmission completes. Doing
/// that from the shared Timer Service task deadlocks it (and every other software timer
/// in the system) rather than just the one blink update. A dedicated task has no such
/// restriction, so it can safely call blocking code like NeoPixel's show().
/// User does not need to call loop() - updates happen automatically.
/// </remarks>
///
class LED : public BasicLED
{
private:

   static void _taskFunc(void* param)
   {
      LED* led = static_cast<LED*>(param);
      while (true)
      {
         led->loop();
         vTaskDelay(pdMS_TO_TICKS(1));
      }
   }

   void _startTask()
   {
      xTaskCreate(
         _taskFunc,     // task function
         "BlinkTask",   // only used for debugging
         4096,          // stack size (bytes)
         this,          // parameter
         1,             // priority
         nullptr);      // task handle (not needed)
   }

public:
   ///
   /// <summary>
   /// Constructs an LED controller on the specified pin (default 0 = no pin).
   /// </summary>
   /// <param name="pin">GPIO pin number for the LED (0 = disabled)</param>
   /// <param name="color">LED color/lens tint used to seed a default calibration factor (see ledColorCalibrationFactor()); defaults to LEDColor::UNKNOWN (no scaling).</param>
   ///
   LED(uint8_t pin = 0, LEDColor color = LEDColor::UNKNOWN) : BasicLED(pin, color)
   {}

   ///
   /// <summary>
   /// Initializes the LED and starts the internal background task for automatic updates.
   /// </summary>
   ///
   void begin()
   {
      BasicLED::begin();

      _startTask();
   }
};

///
/// <summary>


/// RGB LED controller for tri-color LEDs using three GPIO pins.
/// </summary>
/// <remarks>
/// Manages red, green, and blue pins independently with normalized color mixing.
/// Brightness levels are automatically balanced to maintain consistent color intensity.
/// </remarks>
///
class RGBLED : public LED
{
private:
   uint8_t _redPin;
   uint8_t _greenPin;
   uint8_t _bluePin;

   float _redLevel = 1.0f;
   float _greenLevel = 1.0f;
   float _blueLevel = 1.0f;

   float _redCalibrationFactor = ledColorCalibrationFactor(LEDColor::CLEAR_RED);
   float _greenCalibrationFactor = ledColorCalibrationFactor(LEDColor::CLEAR_GREEN);
   float _blueCalibrationFactor = ledColorCalibrationFactor(LEDColor::CLEAR_BLUE);

public:
   ///
   /// <summary>
   /// Constructs an RGB LED controller with specified pins for R, G, B. Each channel's
   /// calibration factor defaults to its respective LED color (see
   /// ledColorCalibrationFactor()), since the red/green/blue dies in an RGB LED
   /// typically have different efficiencies from one another.
   /// </summary>
   /// <param name="redPin">GPIO pin for red channel</param>
   /// <param name="greenPin">GPIO pin for green channel</param>
   /// <param name="bluePin">GPIO pin for blue channel</param>
   ///
   RGBLED(uint8_t redPin, uint8_t greenPin, uint8_t bluePin) : _redPin(redPin), _greenPin(greenPin), _bluePin(bluePin)
   {}

   ///
   /// <summary>
   /// Initializes all three RGB pins for PWM output.
   /// </summary>
   ///
   virtual void begin() override
   {
      LED::begin();
      pinMode(_redPin, OUTPUT);
      digitalWrite(_redPin, LOW);
      pinMode(_greenPin, OUTPUT);
      digitalWrite(_greenPin, LOW);
      pinMode(_bluePin, OUTPUT);
      digitalWrite(_bluePin, LOW);
   }

   ///
   /// <summary>
   /// Applies the current RGB color mix to the GPIO pins.
   /// </summary>
   ///
   virtual void _apply() override
   {
      if (_isOn)
      {
         // normalize rgb levels to total 1
         float r = _redLevel / (_redLevel + _greenLevel + _blueLevel);
         float g = _greenLevel / (_redLevel + _greenLevel + _blueLevel);
         float b = _blueLevel / (_redLevel + _greenLevel + _blueLevel);

         analogWrite(_redPin, _level * _calibrationFactor * _redCalibrationFactor * r);
         analogWrite(_greenPin, _level * _calibrationFactor * _greenCalibrationFactor * g);
         analogWrite(_bluePin, _level * _calibrationFactor * _blueCalibrationFactor * b);
      }
      else
      {
         analogWrite(_redPin, 0);
         analogWrite(_greenPin, 0);
         analogWrite(_bluePin, 0);
      }
   }

   ///
   /// <summary>
   /// Sets the RGB color using normalized channel values.
   /// </summary>
   /// <param name="redLevel">Red channel intensity from 0.0 to 1.0</param>
   /// <param name="greenLevel">Green channel intensity from 0.0 to 1.0</param>
   /// <param name="blueLevel">Blue channel intensity from 0.0 to 1.0</param>
   ///
   void setColor(float redLevel, float greenLevel, float blueLevel)
   {
      _redLevel = constrain(redLevel, 0.0f, 1.0f);
      _greenLevel = constrain(greenLevel, 0.0f, 1.0f);
      _blueLevel = constrain(blueLevel, 0.0f, 1.0f);
      _apply();
   }

#ifdef COLOR_565
   ///
   /// <summary>
   /// Sets the RGB color using a named Color value.
   /// </summary>
   /// <param name="color">Color value</param>
   ///
   void setColor(Color color)
   {
      setColor(
         Color565::getR(color) / 255.0f,
         Color565::getG(color) / 255.0f,
         Color565::getB(color) / 255.0f);
   }
#endif
};

#ifdef ARDUINO_NEOPIXEL_SUPPORTED

///
/// <summary>
/// Number of LEDs managed by a single NeoPixelLED instance.
/// </summary>
///
constexpr uint8_t NUM_LEDS = 1;

// Data pin and color order for the onboard NeoPixel, auto-detected based on the target board.
#if defined(ARDUINO_WAVESHARE_ESP32_S3_ZERO) || defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS) || defined(ARDUINO_WAVESHARE_ESP32S3_ZERO_SENSORS2)
constexpr int16_t NEOPIXEL_LED_PIN = 21;
constexpr neoPixelType NEOPIXEL_LED_TYPE = NEO_RGB + NEO_KHZ800;
#elif defined(ARDUINO_WAVESHARE_ESP32C6_LCD_147)
constexpr int16_t NEOPIXEL_LED_PIN = 8;
constexpr neoPixelType NEOPIXEL_LED_TYPE = NEO_RGB + NEO_KHZ800;
#elif defined(ARDUINO_HOSYOND_ESP32S3_TOUCH_LCD_40)
constexpr int16_t NEOPIXEL_LED_PIN = 42;
constexpr neoPixelType NEOPIXEL_LED_TYPE = NEO_GRB + NEO_KHZ800;
#elif defined(ARDUINO_ESP32S3_DEV_PLAYGROUND) || defined(ARDUINO_ESP32S3_DEV)
constexpr int16_t NEOPIXEL_LED_PIN = 48;
constexpr neoPixelType NEOPIXEL_LED_TYPE = NEO_GRB + NEO_KHZ800;
#else
constexpr int16_t NEOPIXEL_LED_PIN = PIN_NEOPIXEL;
constexpr neoPixelType NEOPIXEL_LED_TYPE = NEO_GRB + NEO_KHZ800;
#endif

///
/// <summary>
/// Manages a multi-pixel WS2812/NeoPixel strip shared across multiple NeoPixelLED
/// instances (e.g. when pixel 0 is a status indicator and pixel 1 is a general-purpose
/// LED on the same data pin).
/// </summary>
/// <remarks>
/// Guards pixel updates and show() calls with a FreeRTOS mutex so independent
/// NeoPixelLED background tasks can safely update their own pixel indices concurrently.
/// </remarks>
///
class NeoPixelStrip
{
private:
   Adafruit_NeoPixel _pixels;
   uint16_t _numLeds;
   int16_t _pin;
   neoPixelType _type;
   SemaphoreHandle_t _mutex = nullptr;
   bool _initialized = false;

public:
   ///
   /// <summary>
   /// Constructs a NeoPixelStrip for a chain of addressable LEDs on a single data pin.
   /// </summary>
   /// <param name="numLeds">Number of NeoPixels in the strip</param>
   /// <param name="pin">GPIO data pin for the strip</param>
   /// <param name="type">NeoPixel color order and frequency flags</param>
   ///
   NeoPixelStrip(uint16_t numLeds, int16_t pin, neoPixelType type = NEO_GRB + NEO_KHZ800)
      : _numLeds(numLeds),
        _pin(pin),
        _type(type)
   {
   }

   ///
   /// <summary>
   /// Gets the GPIO data pin assigned to the strip.
   /// </summary>
   /// <returns>The strip's GPIO data pin.</returns>
   ///
   int16_t pin() const
   {
      return _pin;
   }

   ///
   /// <summary>
   /// Configures and initializes the underlying Adafruit_NeoPixel strip. Safe to call
   /// multiple times; subsequent calls after the first are no-ops.
   /// </summary>
   ///
   void begin()
   {
      if (_initialized)
      {
         return;
      }

      _initialized = true;
      _mutex = xSemaphoreCreateMutex();

#if defined(ESP32) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
      espInit();
#endif

      _pixels.updateType(_type);
      _pixels.updateLength(_numLeds);
      _pixels.setPin(_pin);
      _pixels.begin();
      _pixels.clear();
      _pixels.show();
   }

   ///
   /// <summary>
   /// Updates a single pixel's color in the strip buffer and latches the strip.
   /// </summary>
   /// <param name="pixelIndex">Zero-based index of the pixel in the strip</param>
   /// <param name="r">Scaled red channel value (0-255)</param>
   /// <param name="g">Scaled green channel value (0-255)</param>
   /// <param name="b">Scaled blue channel value (0-255)</param>
   ///
   void setPixelColorAndShow(uint16_t pixelIndex, uint8_t r, uint8_t g, uint8_t b)
   {
      if (_mutex != nullptr)
      {
         xSemaphoreTake(_mutex, portMAX_DELAY);
      }

      _pixels.setPixelColor(pixelIndex, r, g, b);
      _pixels.show();

      if (_mutex != nullptr)
      {
         xSemaphoreGive(_mutex);
      }
   }
};

///
/// <summary>
/// NeoPixel/WS2812B addressable LED controller using the Adafruit_NeoPixel library.
/// </summary>
/// <remarks>
/// Manages either a standalone single NeoPixel LED or one pixel on a shared
/// NeoPixelStrip, with full RGB color control. Defaults to orange (255, 80, 0) so
/// standard LED methods (turnOn(), flash(), blink(), setLevel()) light the pixel even
/// when setColor() has not been called.
/// </remarks>
///
class NeoPixelLED : public LED
{
private:
   // Constructed empty and configured lazily in begin() rather than via the
   // (n, pin, type) constructor. NeoPixelLED instances are commonly created as
   // globals (e.g. board wrapper members), which run their constructors during
   // static initialization - before the ESP-IDF runtime is fully up. On ESP32
   // (IDF >= 5) the (n, pin, type) constructor calls espInit() to claim an RMT
   // channel immediately, which can silently fail that early, leaving the
   // NeoPixel dark. Deferring setup to begin() (called from setup()) avoids this.
   Adafruit_NeoPixel _pixel;
   NeoPixelStrip* _strip = nullptr;
   uint16_t _pixelIndex = 0;
   int16_t _neoPixelPin = NEOPIXEL_LED_PIN;
   neoPixelType _neoPixelType = NEOPIXEL_LED_TYPE;
   uint8_t _red = 255;
   uint8_t _green = 80;
   uint8_t _blue = 0;

public:
   ///
   /// <summary>
   /// Constructs a NeoPixelLED controller using the specified data pin and color order
   /// (defaults to the platform's auto-detected onboard NeoPixel pin and type).
   /// </summary>
   /// <param name="pin">GPIO data pin for the NeoPixel</param>
   /// <param name="type">NeoPixel color order and frequency flags</param>
   ///
   NeoPixelLED(int16_t pin = NEOPIXEL_LED_PIN, neoPixelType type = NEOPIXEL_LED_TYPE)
   {
      _neoPixelPin = pin;
      _neoPixelType = type;
      _level = constrain(255 * 0.05f, 0, 255);
   }

   ///
   /// <summary>
   /// Constructs a NeoPixelLED controller targeting a single pixel on a shared
   /// NeoPixelStrip.
   /// </summary>
   /// <param name="strip">The shared NeoPixelStrip that owns the underlying hardware pin</param>
   /// <param name="pixelIndex">Zero-based index of the pixel controlled by this instance</param>
   /// <param name="color">Optional LED color/calibration preset</param>
   ///
   NeoPixelLED(NeoPixelStrip* strip, uint16_t pixelIndex, LEDColor color = LEDColor::UNKNOWN)
      : LED(0, color),
        _strip(strip),
        _pixelIndex(pixelIndex)
   {
      ASSERT(strip != nullptr);

      _neoPixelPin = strip->pin();
      _level = constrain(255 * 0.5f, 0, 255);
   }

   ///
   /// <summary>
   /// Configures and initializes the Adafruit_NeoPixel library for the NeoPixel LED.
   /// </summary>
   ///
   virtual void begin() override
   {
      if (_strip != nullptr)
      {
         _strip->begin();
         _apply();
         LED::begin();
         return;
      }

#ifdef NEOPIXEL_POWER
      // Some boards (e.g. Adafruit Feather ESP32-S3 TFT) gate power to the onboard
      // NeoPixel behind a separate switched pin, which must be driven on before the
      // NeoPixel will light.
      if (_neoPixelPin == NEOPIXEL_LED_PIN)
      {
         pinMode(NEOPIXEL_POWER, OUTPUT);
         digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
      }
#endif

#if defined(ESP32) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
      // The empty Adafruit_NeoPixel constructor used above (see the comment on
      // _pixel) skips espInit(), which the (n, pin, type) constructor normally
      // calls to create the RMT show_mutex on ESP-IDF >= 5. Without that mutex,
      // espShow() silently no-ops (its "if (show_mutex && ...)" guard fails),
      // so begin() still reports success but the NeoPixel never lights.
      espInit();
#endif

      _pixel.updateType(_neoPixelType);
      _pixel.updateLength(NUM_LEDS);
      _pixel.setPin(_neoPixelPin);
      _pixel.begin();
      _apply(); // start with the led off

      // Start the background blink task only after the pixel buffer and RMT
      // mutex are fully initialized, so _taskFunc can't race with begin() or
      // call _apply() before _pixel is ready.
      LED::begin();
   }

   ///
   /// <summary>
   /// Applies brightness and shows the current LED state.
   /// </summary>
   /// <remarks>
   /// Scales the stored raw color by the current brightness level itself, rather than
   /// relying on Adafruit_NeoPixel::setBrightness(), since that method destructively
   /// rescales the pixel buffer in place and would drift the color over repeated calls
   /// (e.g. from blinking/flashing).
   /// </remarks>
   ///
   virtual void _apply() override
   {
      uint8_t brightness = _isOn ? (uint8_t)(_level * _calibrationFactor) : 0;
      uint8_t r = (uint16_t)_red * brightness / 255;
      uint8_t g = (uint16_t)_green * brightness / 255;
      uint8_t b = (uint16_t)_blue * brightness / 255;

      if (_strip != nullptr)
      {
         _strip->setPixelColorAndShow(_pixelIndex, r, g, b);
      }
      else
      {
         _pixel.setPixelColor(0, r, g, b);
         _pixel.show();
      }
   }

   ///
   /// <summary>
   /// Sets the NeoPixel RGB color using normalized channel values.
   /// </summary>
   /// <param name="redLevel">Red channel intensity from 0.0 to 1.0</param>
   /// <param name="greenLevel">Green channel intensity from 0.0 to 1.0</param>
   /// <param name="blueLevel">Blue channel intensity from 0.0 to 1.0</param>
   ///
   void setColor(float redLevel, float greenLevel, float blueLevel)
   {
      uint8_t r = constrain(255 * redLevel, 0, 255);
      uint8_t g = constrain(255 * greenLevel, 0, 255);
      uint8_t b = constrain(255 * blueLevel, 0, 255);
      setColor(r, g, b);
   }

   ///
   /// <summary>
   /// Sets the NeoPixel RGB color using 8-bit channel values.
   /// </summary>
   /// <param name="r">Red channel value from 0 to 255</param>
   /// <param name="g">Green channel value from 0 to 255</param>
   /// <param name="b">Blue channel value from 0 to 255</param>
   ///
   void setColor(uint8_t r, uint8_t g, uint8_t b)
   {
      _red = r;
      _green = g;
      _blue = b;
      _apply();
   }

#ifdef COLOR_565
   ///
   /// <summary>
   /// Sets the NeoPixel RGB color using a named Color value.
   /// </summary>
   /// <param name="color">Color value</param>
   ///
   void setColor(Color color)
   {
      setColor(
         Color565::getR(color),
         Color565::getG(color),
         Color565::getB(color));
   }
#endif
};

#endif // ARDUINO_NEOPIXEL_SUPPORTED
