#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

///
/// <summary>
/// Provides a shared recursive FreeRTOS mutex for serializing access to the Arduino
/// TwoWire (Wire) I2C bus when accessed concurrently from both the main loop (e.g.,
/// capacitive touch polling) and background timer tasks (e.g., 860 Hz ADS1115 sampling).
/// </summary>
///
class I2CMutex
{
public:
   ///
   /// <summary>
   /// Returns the singleton FreeRTOS recursive mutex protecting the shared I2C bus.
   /// </summary>
   /// <returns>SemaphoreHandle_t for the shared I2C bus mutex.</returns>
   ///
   static SemaphoreHandle_t handle()
   {
      static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
      return mutex;
   }

   ///
   /// <summary>
   /// Acquires the shared I2C bus lock, waiting up to timeoutTicks.
   /// </summary>
   /// <param name="timeoutTicks">Maximum ticks to wait (default portMAX_DELAY).</param>
   /// <returns>True if the lock was acquired; false on timeout.</returns>
   ///
   static bool lock(TickType_t timeoutTicks = portMAX_DELAY)
   {
      SemaphoreHandle_t m = handle();
      return m == nullptr || xSemaphoreTakeRecursive(m, timeoutTicks) == pdTRUE;
   }

   ///
   /// <summary>
   /// Releases the shared I2C bus lock.
   /// </summary>
   ///
   static void unlock()
   {
      SemaphoreHandle_t m = handle();
      if (m != nullptr)
      {
         xSemaphoreGiveRecursive(m);
      }
   }
};

///
/// <summary>
/// RAII guard that acquires I2CMutex for the duration of a scope.
/// </summary>
///
class I2CScopedLock
{
private:
   bool _locked = false;

public:
   ///
   /// <summary>
   /// Acquires the shared I2C bus lock.
   /// </summary>
   /// <param name="timeoutTicks">Maximum ticks to wait (default portMAX_DELAY).</param>
   ///
   explicit I2CScopedLock(TickType_t timeoutTicks = portMAX_DELAY)
   {
      _locked = I2CMutex::lock(timeoutTicks);
   }

   ///
   /// <summary>
   /// Releases the shared I2C bus lock if held.
   /// </summary>
   ///
   ~I2CScopedLock()
   {
      if (_locked)
      {
         I2CMutex::unlock();
      }
   }

   ///
   /// <summary>
   /// Returns true if the lock was acquired.
   /// </summary>
   /// <returns>True if locked.</returns>
   ///
   bool locked() const
   {
      return _locked;
   }
};
