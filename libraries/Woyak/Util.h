#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <cstring>
#include <string>
#include "Format.h"

#if !defined ( BOARD_HAS_PIN_REMAP ) && !defined ( digitalPinToGPIONumber )
 #define digitalPinToGPIONumber(pin) (pin)
#endif

///
/// <summary>
/// Halts the device (via Util::setHaltReason()/Util::reset()) if condition is false,
/// recording the failed expression, file, and line so it's visible via
/// Util::checkTheLastShutdownReason() on the next boot. Intended for invariants that should never be
/// false at runtime (e.g. a pointer that must always be set): failing loudly and
/// immediately makes bugs visible instead of letting the sketch silently limp along with a
/// no-op guard hiding the underlying problem.
/// </summary>
///
#define ASSERT(condition) \
   do \
   { \
      if (!(condition)) \
      { \
         Util::reset(0.0f, String("Assertion failed: ") + #condition + " (" __FILE__ ":" + String(__LINE__) + ")"); \
      } \
   } while (0)

///
/// <summary>
/// Returns the number of elements in a fixed-size array, computed at compile time.
/// </summary>
///
template <typename T, size_t N>
constexpr size_t arraySize(const T (&)[N])
{
   return N;
}

#undef ARRAY_SIZE
#define ARRAY_SIZE(array) arraySize(array)

///
/// <summary>
/// Set true once a boot has been running long enough (see Util::SAFE_MODE_GRACE_PERIOD_S)
/// to be considered stable, so a later crash doesn't trigger safe mode. Lives in RTC "noinit"
/// memory so it survives the panic/watchdog reset itself (unlike regular globals, which are
/// reinitialized), but is still cleared by a power-on/brownout reset, which is desired since
/// that's an entirely fresh boot attempt.
/// </summary>
///
RTC_NOINIT_ATTR static bool _util_pastSafeModeGracePeriod;

/// <summary>Magic value confirming that _util_setupStep was initialized prior to a watchdog reset.</summary>
static constexpr uint32_t _UTIL_SETUP_STEP_MAGIC = 0x574F594B;

/// <summary>Guards _util_setupStep against uninitialized RTC memory after power-on/brownout resets.</summary>
RTC_NOINIT_ATTR static uint32_t _util_setupStepMagic;

/// <summary>True while setup is in progress and the setup watchdog is tracking initialization steps.</summary>
RTC_NOINIT_ATTR static bool _util_inSetup;

/// <summary>Name of the most recent initialization step started before a setup watchdog reset.</summary>
RTC_NOINIT_ATTR static char _util_setupStep[64];

/// <summary>
/// Utility functions for common operations.
/// </summary>
class Util
{
private:
   /// <summary>True once armSetupWatchdog() has subscribed the current task on this boot.</summary>
   static inline bool _setupWatchdogArmed = false;

   /// <summary>True if the previous boot was still in setup when it reset, captured the first time armSetupWatchdog() is called on this boot before _util_inSetup is overwritten.</summary>
   static inline bool _wasInSetupOnBoot = false;

public:
   ///
   /// <summary>
   /// Seconds a boot must run past begin() before a subsequent crash is considered
   /// "recovered" (restart normally) rather than a startup failure (enter safe mode). See
   /// wasPastSafeModeGracePeriod()/isPastSafeModeGracePeriod().
   /// </summary>
   ///
   static constexpr float SAFE_MODE_GRACE_PERIOD_S = 30.0f;

   ///
   /// <summary>
   /// Seconds a required configuration prompt (no saved default) waits for user input
   /// before resetting the device into safe mode so it doesn't stay stranded offline.
   /// </summary>
   ///
   static constexpr uint16_t REQUIRED_PROMPT_TIMEOUT_S = 60;

   ///
   /// <summary>
   /// Seconds of inactivity allowed within any individual initialization step before the
   /// task watchdog resets the device into safe mode. Advancing to the next step (via
   /// setSetupStep()) or recording progress (via feedSetupWatchdog()) renews this window.
   /// </summary>
   ///
   static constexpr uint8_t SETUP_WATCHDOG_TIMEOUT_S = 60;

   /// <summary>Halt reason recorded when a required configuration prompt times out waiting for user input.</summary>
   static constexpr const char* PROMPT_TIMEOUT_HALT_REASON = "Configuration prompt timed out";

   /// <summary>Halt reason recorded when a required configuration prompt cannot be shown because no Serial monitor is attached.</summary>
   static constexpr const char* PROMPT_NO_SERIAL_HALT_REASON = "Configuration prompt requires Serial connection";

   /// <summary>Halt reason recorded when Safe Mode is explicitly requested via a remote command.</summary>
   static constexpr const char* SAFE_MODE_REQUESTED_HALT_REASON = "Safe Mode requested remotely";

   ///
   /// <summary>
   /// Returns whether the previous boot was still in setup (i.e. completeSetupWatchdog() had
   /// not yet been called) when it reset. Safe to call both before and after
   /// armSetupWatchdog() has been called on the current boot.
   /// </summary>
   /// <returns>True if the previous boot reset while setup was still in progress.</returns>
   ///
   static bool wasInSetup()
   {
      if (_setupWatchdogArmed)
      {
         return _wasInSetupOnBoot;
      }

      return _util_setupStepMagic == _UTIL_SETUP_STEP_MAGIC && _util_inSetup;
   }

   ///
   /// <summary>
   /// Returns the initialization step that was in progress if the previous boot was reset
   /// by the task watchdog while still in setup, or an empty string otherwise. Call before
   /// armSetupWatchdog() (which overwrites the recorded step for the current boot).
   /// </summary>
   /// <returns>The hung initialization step name, or an empty string.</returns>
   ///
   static String lastSetupWatchdogStep()
   {
      if (esp_reset_reason() == ESP_RST_TASK_WDT &&
          wasInSetup() &&
          _util_setupStep[0] != '\0')
      {
         _util_setupStep[sizeof(_util_setupStep) - 1] = '\0';
         return String(_util_setupStep);
      }

      return "";
   }

   ///
   /// <summary>
   /// Arms the ESP32 task watchdog at the start of initialization with a per-step
   /// inactivity timeout of SETUP_WATCHDOG_TIMEOUT_S seconds, and records the initial
   /// step name in RTC noinit memory so a hang during setup can be identified on the next
   /// boot. Safe to call more than once (e.g. from SerialX::begin() and SketchBase).
   /// </summary>
   /// <param name="initialStep">Initial setup step label to record.</param>
   ///
   static void armSetupWatchdog(const char* initialStep = "startup")
   {
      if (!_setupWatchdogArmed)
      {
         _wasInSetupOnBoot = _util_setupStepMagic == _UTIL_SETUP_STEP_MAGIC && _util_inSetup;
      }

      _util_setupStepMagic = _UTIL_SETUP_STEP_MAGIC;
      _util_inSetup = true;
      setSetupStep(initialStep);

      if (!_setupWatchdogArmed)
      {
         esp_task_wdt_config_t twdtConfig = {
            .timeout_ms = SETUP_WATCHDOG_TIMEOUT_S * 1000U,
            .idle_core_mask = 0,
            .trigger_panic = true,
         };
         esp_task_wdt_reconfigure(&twdtConfig);
         if (esp_task_wdt_status(nullptr) != ESP_OK)
         {
            esp_task_wdt_add(nullptr);
         }
         _setupWatchdogArmed = true;
      }

      esp_task_wdt_reset();
   }

   ///
   /// <summary>
   /// Records the name of the initialization step currently starting (stripping any
   /// trailing "... " suffix used by display/log init labels) and feeds the setup
   /// watchdog so the new step gets a full SETUP_WATCHDOG_TIMEOUT_S window.
   /// </summary>
   /// <param name="step">Name or label of the initialization step starting now.</param>
   ///
   static void setSetupStep(const char* step)
   {
      if (step != nullptr && _util_setupStepMagic == _UTIL_SETUP_STEP_MAGIC)
      {
         size_t len = strlen(step);
         while (len > 0 && (step[len - 1] == ' ' || step[len - 1] == '.'))
         {
            len--;
         }
         if (len >= sizeof(_util_setupStep))
         {
            len = sizeof(_util_setupStep) - 1;
         }
         memcpy(_util_setupStep, step, len);
         _util_setupStep[len] = '\0';
      }

      feedSetupWatchdog();
   }

   ///
   /// <summary>
   /// Feeds the task watchdog if it has been armed, renewing the current step's
   /// inactivity window during long-running or interactive setup operations.
   /// </summary>
   ///
   static void feedSetupWatchdog()
   {
      if (_setupWatchdogArmed)
      {
         esp_task_wdt_reset();
      }
   }

   ///
   /// <summary>
   /// Marks setup as complete for watchdog step-tracking purposes and feeds the watchdog.
   /// </summary>
   ///
   static void completeSetupWatchdog()
   {
      _util_inSetup = false;
      if (_util_setupStepMagic == _UTIL_SETUP_STEP_MAGIC)
      {
         _util_setupStep[0] = '\0';
      }
      feedSetupWatchdog();
   }

   ///
   /// <summary>
   /// Clears the safe-mode grace-period flag. Call once at the very start of begin(), after
   /// checking Util::wasPastSafeModeGracePeriod(), so this boot starts tracking its own
   /// uptime from a clean state.
   /// </summary>
   ///
   static void clearSafeModeGracePeriod()
   {
      _util_pastSafeModeGracePeriod = false;
   }

   ///
   /// <summary>
   /// Returns whether the *previous* boot had survived past Util::SAFE_MODE_GRACE_PERIOD_S
   /// before this boot started, i.e. whether a crash that led to this boot happened after the
   /// sketch had already proven itself stable. Must be called before
   /// Util::clearSafeModeGracePeriod(), at the very start of begin().
   /// </summary>
   /// <returns>True if the previous boot had already passed the grace period.</returns>
   ///
   static bool wasPastSafeModeGracePeriod()
   {
      return _util_pastSafeModeGracePeriod;
   }

   ///
   /// <summary>
   /// Returns true once the current boot has survived past Util::SAFE_MODE_GRACE_PERIOD_S,
   /// meaning a crash from this point on should just restart normally instead of entering
   /// safe mode. Must be polled periodically (e.g. from the main loop) since there's no
   /// background timer; checks elapsed time via millis() the first time it's called after
   /// the grace period has elapsed, then remembers the result.
   /// </summary>
   /// <returns>True if the grace period has already elapsed (or elapsed as of this call).</returns>
   ///
   static bool isPastSafeModeGracePeriod()
   {
      if (!_util_pastSafeModeGracePeriod && millis() >= static_cast<unsigned long>(SAFE_MODE_GRACE_PERIOD_S * 1000.0f))
      {
         _util_pastSafeModeGracePeriod = true;
      }

      return _util_pastSafeModeGracePeriod;
   }

   /// <summary>
   /// Returns a random float value that fits within the given format, i.e. one whose
   /// whole-number digit count doesn't exceed what the format can display (e.g. a
   /// format of "####.#" returns a value in the range [0, 9999.9]).
   /// </summary>
   /// <param name="format">Format describing the target field's length and precision.</param>
   /// <returns>A random value between 0 and the largest value the format can display.</returns>
   static float randomValue(const Format& format)
   {
      uint32_t scale = 1;
      for (uint8_t p = 0; p < format.precision(); p++)
      {
         scale *= 10;
      }

      // Whole-number digit count is the format's total length minus its decimal digits
      // and the decimal point itself (e.g. "####.#" has 4 whole digits).
      size_t wholeDigits = format.length() - format.precision() - 1;
      uint32_t maxWhole = 1;
      for (size_t d = 0; d < wholeDigits; d++)
      {
         maxWhole *= 10;
      }

          return (float)random(0, (long)(maxWhole * scale) - 1) / (float)scale;
      }

      /// <summary>
      /// Returns a random float value that fits within the given format string, i.e. one whose
      /// whole-number digit count doesn't exceed what the format can display (e.g. a
      /// format of "####.#" returns a value in the range [0, 9999.9]).
      /// </summary>
      /// <param name="format">Format string describing the target field's length and precision.</param>
      /// <returns>A random value between 0 and the largest value the format can display.</returns>
      static float randomValue(const char* format)
      {
         return randomValue(Format(format));
      }

      /// <summary>
      /// Converts an ESP32 reset reason code to a human-readable string.
   /// </summary>
   /// <param name="reason">Reset reason to convert; defaults to the current boot's reason</param>
   /// <returns>Human-readable description of the reset reason</returns>
   static const char* resetReasonString(esp_reset_reason_t reason = esp_reset_reason())
   {
      switch (reason)
      {
      case ESP_RST_POWERON:
         return "Power-on reset";
      case ESP_RST_EXT:
         return "External pin reset";
      case ESP_RST_SW:
         return "Software reset";
      case ESP_RST_PANIC:
         return "Software panic reset";
      case ESP_RST_INT_WDT:
         return "Interrupt watchdog reset";
      case ESP_RST_TASK_WDT:
         return "Task watchdog reset";
      case ESP_RST_WDT:
         return "Other watchdog reset";
      case ESP_RST_DEEPSLEEP:
         return "Woke from deep sleep";
      case ESP_RST_BROWNOUT:
         return "Brownout reset (voltage dropout)";
      case ESP_RST_SDIO:
         return "SDIO reset";
      default:
         return "Unknown reset reason";
      }
   }

   /// <summary>
   /// Calculates elapsed time between two clock readings, handling wraparound.
   /// Uses unsigned arithmetic: if end &lt; start (clock wrapped), the result
   /// is still correct due to modulo 2^32 arithmetic.
   /// </summary>
   /// <param name="start">Start timestamp from micros() or millis()</param>
   /// <param name="end">End timestamp from micros() or millis()</param>
   /// <returns>Elapsed time in the same units as the input timestamps</returns>
   static unsigned long IRAM_ATTR getSpan(unsigned long start, unsigned long end)
   {
      return end - start;
   }

   /// <summary>
   /// Calculates microseconds elapsed since the given starting time.
   /// </summary>
   /// <param name="start">Starting timestamp from a prior micros() call</param>
   /// <returns>Microseconds elapsed</returns>
   static unsigned long microsSince(unsigned long start)
   {
      return getSpan(start, micros());
   }

   /// <summary>
   /// Formats a floating-point value to a target number of significant digits.
   /// </summary>
   /// <param name="value">Value to format.</param>
   /// <param name="significantDigits">Number of significant digits to preserve.</param>
   /// <returns>Formatted numeric text, or "n/a" for non-finite input.</returns>
   static String toSignificantString(float value, uint8_t significantDigits)
   {
      if (!isfinite(value))
      {
         return "n/a";
      }

      if (value == 0.0f)
      {
         return "0";
      }

      float absValue = fabsf(value);
      int exponent = static_cast<int>(floorf(log10f(absValue)));
      int decimals = static_cast<int>(significantDigits) - 1 - exponent;
      if (decimals <= 0)
      {
         long truncated = static_cast<long>(value);
         return String(truncated);
      }

      return String(value, static_cast<unsigned int>(decimals));
   }


   /// <summary>
   /// Optional callback invoked by reset() just before the device restarts (e.g. to report a
   /// RESTARTING state to the DeviceServer). Set by DeviceServerClient::begin().
   /// </summary>
   static inline void (*onResetting)() = nullptr;

   /// <summary>
   /// Returns true if the given reset reason indicates the previous boot crashed (panic or
   /// watchdog timeout) rather than an intentional restart. Util::reset() always restarts via
   /// deep sleep (ESP_RST_DEEPSLEEP) or ESP.restart() (ESP_RST_SW), so neither overlaps with
   /// these crash reasons; callers can use this to trigger a safe-mode boot that bypasses
   /// risky sketch-specific initialization and connects only to WiFi/the Device Server.
   /// </summary>
   /// <param name="reason">Reset reason to classify; defaults to the current boot's reason</param>
   /// <returns>True if the reason is ESP_RST_PANIC, ESP_RST_TASK_WDT, ESP_RST_INT_WDT, or ESP_RST_WDT</returns>
   static bool isCrashResetReason(esp_reset_reason_t reason = esp_reset_reason())
   {
      switch (reason)
      {
      case ESP_RST_PANIC:
      case ESP_RST_TASK_WDT:
      case ESP_RST_INT_WDT:
      case ESP_RST_WDT:
         return true;
      default:
         return false;
      }
   }

   ///
   /// <summary>
   /// Returns whether a recorded halt reason (see setHaltReason()/checkTheLastShutdownReason())
   /// came from a failed ASSERT. An ASSERT failure resets via deep sleep (see reset()), so the
   /// hardware reset reason on the next boot is ESP_RST_DEEPSLEEP rather than one of the panic/
   /// watchdog reasons isCrashResetReason() checks for; without this, an ASSERT failure during
   /// early init would never be classified as a crash and so would never trigger safe mode,
   /// letting the device reboot-loop into the same assertion forever instead of falling back to
   /// safe mode.
   /// </summary>
   /// <param name="haltReason">Halt reason recorded for the previous boot (e.g. via SerialX::lastShutdownReason()).</param>
   /// <returns>True if haltReason was recorded by a failed ASSERT.</returns>
   ///
   static bool isAssertHaltReason(const String& haltReason)
   {
      return haltReason.startsWith("Assertion failed:");
   }

   ///
   /// <summary>
   /// Returns whether a recorded halt reason should trigger safe mode on the next boot
   /// (a failed ASSERT, or an unanswered required configuration prompt).
   /// </summary>
   /// <param name="haltReason">Halt reason recorded for the previous boot (e.g. via SerialX::lastShutdownReason()).</param>
   /// <returns>True if haltReason warrants entering safe mode.</returns>
   ///
   static bool isSafeModeHaltReason(const String& haltReason)
   {
      return isAssertHaltReason(haltReason) ||
         haltReason == PROMPT_TIMEOUT_HALT_REASON ||
         haltReason == PROMPT_NO_SERIAL_HALT_REASON ||
         haltReason == SAFE_MODE_REQUESTED_HALT_REASON;
   }

   /// <summary>
   /// Resets the device after an optional delay, recording a reason so it's reported
   /// as the "Reset reason" (in place of the generic hardware reset reason) via
   /// checkTheLastShutdownReason() on the next boot.
   /// </summary>
   /// <param name="delaySecs">Seconds to delay before reset (default 0.0)</param>
   /// <param name="reason">Reason for the reset (e.g. "Influx failed to begin" or "Scheduled daily reboot"), recorded via setHaltReason() and reported as the next boot's "Reset reason".</param>
   /// <remarks>
   /// This function does not return.
   /// </remarks>
   static void reset(float delaySecs, const String& reason)
   {
      setHaltReason(reason);

      if (onResetting != nullptr)
      {
         onResetting();
      }

      delay(static_cast<unsigned long>(1000.0f * delaySecs));

      // Deep sleep gives a more complete hardware-level reset than ESP.restart() (a soft
      // restart of the running app) - most of the chip is powered down before waking and
      // re-running setup().
      esp_sleep_enable_timer_wakeup(1); // wake up almost immediately
      esp_deep_sleep_start();
   }

   /// <summary>
   /// Overload of reset() accepting a std::string reason, since this codebase builds
   /// many reset reasons via std::string concatenation (Arduino's String class doesn't
   /// implicitly convert from std::string).
   /// </summary>
   /// <param name="delaySecs">Seconds to delay before reset</param>
   /// <param name="reason">Reason for the reset, recorded via setHaltReason()</param>
   /// <remarks>
   /// This function does not return.
   /// </remarks>
   static void reset(float delaySecs, const std::string& reason)
   {
      reset(delaySecs, String(reason.c_str()));
   }

   /// <summary>
   /// Overload of reset() accepting a raw C string reason.
   /// </summary>
   /// <param name="delaySecs">Seconds to delay before reset</param>
   /// <param name="reason">Reason for the reset, recorded via setHaltReason()</param>
   /// <remarks>
   /// This function does not return.
   /// </remarks>
   static void reset(float delaySecs, const char* reason)
   {
      reset(delaySecs, String(reason));
   }

   /// <summary>
   /// Saves a halt reason to a preferences object.
   /// </summary>
   static void setHaltReason(const String& reason)
   {
      Preferences preferences;
      preferences.begin("Device", false);
      preferences.putString("halt", reason);
      preferences.end();
   }

   /// <summary>
   /// Checks for a previous halt reason recorded in a preferences object. Does not log
   /// directly itself, since Logger depends on DeviceServerClient, which depends on Timer,
   /// which depends on this file; callers should log the result themselves (see
   /// SerialX::begin()).
   /// </summary>
   /// <returns>The previous halt reason, or an empty string if none was recorded.</returns>
   static String checkTheLastShutdownReason()
   {
      String reason = "";
      Preferences preferences;
      preferences.begin("Device", false);
      // Note: Not all implementation of Preferences have isKey, but ESP32 does.
      if (preferences.isKey("halt"))
      {
         reason = preferences.getString("halt", "");
         preferences.remove("halt");
      }
      preferences.end();

      return reason;
   }

   ///
   /// <summary>
   /// Prints known information about the current board and chip to serial.
   /// </summary>
   ///
   static void printBoardInfo()
   {
      Serial.println("=== Board Info ===");
      Serial.print("Board:            "); Serial.println(ARDUINO_BOARD);
      Serial.print("Chip Model:       "); Serial.println(ESP.getChipModel());
      Serial.print("Chip Revision:    "); Serial.println(ESP.getChipRevision());
      Serial.print("CPU Cores:        "); Serial.println(ESP.getChipCores());
      Serial.print("CPU Freq:         "); Serial.print(ESP.getCpuFreqMHz()); Serial.println(" MHz");
      Serial.print("Flash Size:       "); Serial.print(ESP.getFlashChipSize()); Serial.println(" bytes");
      Serial.print("Flash Speed:      "); Serial.print(ESP.getFlashChipSpeed()); Serial.println(" Hz");
      Serial.print("Heap Size:        "); Serial.print(ESP.getHeapSize()); Serial.println(" bytes");
      Serial.print("Free Heap:        "); Serial.print(ESP.getFreeHeap()); Serial.println(" bytes");
      Serial.print("PSRAM Size:       "); Serial.print(ESP.getPsramSize()); Serial.println(" bytes");
      Serial.print("Free PSRAM:       "); Serial.print(ESP.getFreePsram()); Serial.println(" bytes");
      Serial.print("SDK Version:      "); Serial.println(ESP.getSdkVersion());
      Serial.println("==================");

      Serial.println("=== Default SPI Pins ===");
      Serial.print("SCK:              "); Serial.print(SCK); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SCK)); Serial.println(")");
      Serial.print("MOSI:             "); Serial.print(MOSI); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(MOSI)); Serial.println(")");
      Serial.print("MISO:             "); Serial.print(MISO); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(MISO)); Serial.println(")");
      Serial.print("SS:               "); Serial.print(SS); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SS)); Serial.println(")");
      Serial.println("=========================");

      Serial.println("=== Default I2C Pins ===");
      Serial.print("SDA:              "); Serial.print(SDA); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SDA)); Serial.println(")");
      Serial.print("SCL:              "); Serial.print(SCL); Serial.print(" (GPIO "); Serial.print(digitalPinToGPIONumber(SCL)); Serial.println(")");
      Serial.println("=========================");
   }

};
