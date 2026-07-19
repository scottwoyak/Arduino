#pragma once

#include <Arduino.h>
#include "IEncoder.h"
#include "QuadratureEncoder.h"
#include "ModulinoEncoder.h"

///
/// <summary>
/// Rotary encoder wrapper that wraps position around a specified item count.
/// </summary>
/// <remarks>
/// Wraps an existing IEncoder (e.g. QuadratureEncoder or ModulinoEncoder) to provide
/// circular list navigation. Automatically wraps position to keep it within the range
/// [0, numItems), useful for menu selection and cyclic index traversal. Alternatively,
/// can construct and own its own QuadratureEncoder or ModulinoEncoder backend directly.
/// </remarks>
///
class IndexedEncoder
{
private:
   // Set when this instance created its own backend encoder, so it can be freed in
   // the destructor. Left null when wrapping an externally-owned encoder.
   IEncoder* _ownedEncoder = nullptr;
   IEncoder& _encoder;
   uint16_t _numItems;

public:
   ///
   /// <summary>
   /// Constructs an IndexedEncoder wrapping an existing encoder.
   /// </summary>
   /// <param name="encoder">The underlying encoder to wrap.</param>
   /// <param name="numItems">Number of items in the circular list</param>
   ///
   IndexedEncoder(IEncoder& encoder, uint16_t numItems) : _encoder(encoder), _numItems(numItems)
   {
   }

   ///
   /// <summary>
   /// Constructs an IndexedEncoder that creates and owns a direct GPIO
   /// QuadratureEncoder backend.
   /// </summary>
   /// <param name="pinA">GPIO pin for encoder phase A</param>
   /// <param name="pinB">GPIO pin for encoder phase B</param>
   /// <param name="buttonPin">GPIO pin for encoder pushbutton</param>
   /// <param name="numItems">Number of items in the circular list</param>
   ///
   IndexedEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, uint16_t numItems)
      : _ownedEncoder(new QuadratureEncoder(pinA, pinB, buttonPin)),
        _encoder(*_ownedEncoder),
        _numItems(numItems)
   {
   }

   ///
   /// <summary>
   /// Constructs an IndexedEncoder that creates and owns a ModulinoEncoder
   /// (Modulino Knob module) backend.
   /// </summary>
   /// <param name="numItems">Number of items in the circular list</param>
   /// <param name="address">I2C address of the Modulino Knob module; 0xFF for auto-discovery</param>
   /// <param name="hubPort">Optional Modulino hub port the module is connected through</param>
   ///
   IndexedEncoder(uint16_t numItems, uint8_t address = 0xFF, ModulinoHubPort* hubPort = nullptr)
      : _ownedEncoder(new ModulinoEncoder(address, hubPort)),
        _encoder(*_ownedEncoder),
        _numItems(numItems)
   {
   }

   ///
   /// <summary>
   /// Constructs an IndexedEncoder that creates and owns a ModulinoEncoder
   /// connected through the specified hub port.
   /// </summary>
   /// <param name="hubPort">Modulino hub port the module is connected through</param>
   /// <param name="numItems">Number of items in the circular list</param>
   /// <param name="address">I2C address of the Modulino Knob module; 0xFF for auto-discovery</param>
   ///
   IndexedEncoder(ModulinoHubPort* hubPort, uint16_t numItems, uint8_t address = 0xFF)
      : _ownedEncoder(new ModulinoEncoder(hubPort, address)),
        _encoder(*_ownedEncoder),
        _numItems(numItems)
   {
   }

   // Owns a raw pointer to its backend encoder when it created one itself, so
   // copying/assigning would create a double-delete or dangling reference.
   IndexedEncoder(const IndexedEncoder&) = delete;
   IndexedEncoder& operator=(const IndexedEncoder&) = delete;

   ~IndexedEncoder()
   {
      delete _ownedEncoder;
   }

   ///
   /// <summary>
   /// Initializes the underlying encoder hardware. Call once during setup().
   /// </summary>
   ///
   void begin()
   {
      _encoder.begin();
   }

   ///
   /// <summary>
   /// Sets the current index and updates the item count.
   /// </summary>
   /// <param name="index">The current index (will be wrapped to [0, numItems))</param>
   /// <param name="numItems">The number of items in the list</param>
   ///
   void setIndex(uint16_t index, uint16_t numItems)
   {
      _encoder.setPosition(index);
      _numItems = numItems;
   }

   ///
   /// <summary>
   /// Gets the current index, automatically wrapped to the item count.
   /// </summary>
   /// <returns>Index in range [0, numItems)</returns>
   ///
   uint16_t getIndex() const
   {
      int32_t value = _encoder.getPosition();

      while (value < 0)
      {
         value += _numItems;
      }

      return value % _numItems;
   }
};

