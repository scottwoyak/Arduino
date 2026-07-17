#pragma once

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "DisplayBuffer.h"
#include "Format.h"
#include "IScatterPlot.h"
#include "Structs.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// A single data series owned and rendered by a ScatterPlot: stores its own (x, y)
/// points, display flags (points, connected lines, moving-average line), and color.
/// Also owns a centered moving-average of its own points, computed on demand from
/// movingSampleSize (in the same units as the x values) and cached until new points
/// are added. Set finalized once no more points will be added so the moving average's
/// trailing samples (which need future points to center on) are computed from whatever
/// data actually exists instead of being left pending.
/// </summary>
///
class ScatterPlotSeries : public IScatterPlotSeries
{
   friend class ScatterPlot;

private:
   float* _x = nullptr;
   float* _y = nullptr;
   size_t _count = 0;
   size_t _capacity = 0;

   // Auto-incremented X value used by the single-argument add(value) overload, so callers
   // that don't track their own X coordinate (matching TimedScatterPlotSeries::add()) can
   // still use this series through the IScatterPlotSeries interface.
   float _nextX = 0.0f;

   // Incrementally maintained raw X/Y range, updated in O(1) by add()/set(), so callers
   // like ScatterPlot::_computeAxisRange() don't need to rescan every point on every
   // render() call to find the current range.
   float _xMin = NAN;
   float _xMax = NAN;
   float _yMin = NAN;
   float _yMax = NAN;

   // Incrementally maintained running sum/sum-of-squares/finite-count of the raw Y values,
   // updated in O(1) by add(), so stdDevRange() can compute the mean/stddev without
   // rescanning every point on every call (e.g. every render() while showStdDevBand is set).
   // set() marks _statsDirty since it can modify an arbitrary existing point, forcing one
   // full recompute from scratch the next time stdDevRange() is called.
   float _sumY = 0.0f;
   float _sumSquaresY = 0.0f;
   size_t _finiteYCount = 0;
   bool _statsDirty = false;

   // Set by set() (which can modify an arbitrary existing point, potentially invalidating
   // a previously-tracked min/max) to force one full rescan of _xMin/_xMax/_yMin/_yMax the
   // next time getRawRange() is called, instead of maintaining the range incrementally
   // through that rare path.
   bool _rangeDirty = false;

   // Scratch buffer holding the most recently computed centered moving average, recomputed
   // on demand (and cached until add()/set() invalidates it) from the raw points, movingSampleSize,
   // and finalized.
   float* _movingAverageBuffer = nullptr;
   size_t _movingAverageBufferCapacity = 0;
   size_t _movingAverageReadyCount = 0;
   bool _movingAverageDirty = true;

   // Scratch buffers holding the most recently computed rolling mean +/- stddev band,
   // windowed the same way as the centered moving average (movingSampleSize), so the band
   // moves with the data instead of being a single flat mean/stddev over the whole series.
   // Recomputed on demand (and cached until add()/set() invalidates it).
   float* _stdDevLowBuffer = nullptr;
   float* _stdDevHighBuffer = nullptr;
   size_t _stdDevBufferCapacity = 0;
   size_t _stdDevReadyCount = 0;
   bool _stdDevDirty = true;

   ///
   /// <summary>
   /// Ensures the internal moving-average scratch buffer can hold at least the given
   /// number of samples, growing (and never shrinking) it as needed.
   /// </summary>
   /// <param name="count">Minimum required buffer capacity.</param>
   /// <returns>True if the buffer has at least the requested capacity.</returns>
   ///
   bool _ensureMovingAverageBuffer(size_t count)
   {
      if (count <= _movingAverageBufferCapacity)
      {
         return _movingAverageBuffer != nullptr;
      }

      delete[] _movingAverageBuffer;
      _movingAverageBuffer = new (std::nothrow) float[count];
      _movingAverageBufferCapacity = (_movingAverageBuffer != nullptr) ? count : 0;

      return _movingAverageBuffer != nullptr;
   }

   ///
   /// <summary>
   /// Recomputes the centered moving average into the internal scratch buffer if it has
   /// been invalidated since the last computation. Each output value is the average of
   /// every point whose x falls within movingSampleSize/2 of that point's own x,
   /// blending data from both before and after it. Because centering a point requires
   /// points that arrive after it, a point can only be finalized once enough later points
   /// actually exist; unless finalized is true, trailing points are left pending (NAN).
   /// </summary>
   ///
   void _recomputeMovingAverage()
   {
      if (!_movingAverageDirty)
      {
         return;
      }

      if (_count == 0 || !_ensureMovingAverageBuffer(_count))
      {
         _movingAverageReadyCount = 0;
         _movingAverageDirty = false;
         return;
      }

      float halfWindow = movingSampleSize / 2.0f;

      size_t lo = 0;
      size_t hi = 0;
      float sum = 0.0f;
      size_t finiteCount = 0;
      size_t readyCount = 0;

      for (size_t i = 0; i < _count; i++)
      {
         // Evict points that have aged out of the trailing (past) edge of the window.
         while (lo < _count && (_x[i] - _x[lo]) > halfWindow)
         {
            if (isfinite(_y[lo]))
            {
               sum -= _y[lo];
               finiteCount--;
            }
            lo++;
         }

         // Fold in points up to the leading (future) edge of the window.
         while (hi < _count && (_x[hi] - _x[i]) <= halfWindow)
         {
            if (isfinite(_y[hi]))
            {
               sum += _y[hi];
               finiteCount++;
            }
            hi++;
         }

         // The window is only known to be fully populated once a point beyond its future
         // edge has actually been seen (hi < _count); otherwise more data may still arrive
         // that belongs inside it, unless the series has been marked finalized.
         if (!finalized && (hi >= _count))
         {
            break;
         }

         _movingAverageBuffer[i] = (finiteCount > 0) ? (sum / finiteCount) : NAN;
         readyCount = i + 1;
      }

      for (size_t i = readyCount; i < _count; i++)
      {
         _movingAverageBuffer[i] = NAN;
      }

         _movingAverageReadyCount = readyCount;
         _movingAverageDirty = false;
      }

      ///
      /// <summary>
      /// Ensures the internal stddev-band scratch buffers can hold at least the given number
      /// of samples, growing (and never shrinking) them as needed.
      /// </summary>
      /// <param name="count">Minimum required buffer capacity.</param>
      /// <returns>True if both buffers have at least the requested capacity.</returns>
      ///
      bool _ensureStdDevBuffers(size_t count)
      {
         if (count <= _stdDevBufferCapacity)
         {
            return (_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr);
         }

         delete[] _stdDevLowBuffer;
         delete[] _stdDevHighBuffer;
         _stdDevLowBuffer = new (std::nothrow) float[count];
         _stdDevHighBuffer = new (std::nothrow) float[count];
         _stdDevBufferCapacity = ((_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr)) ? count : 0;

         return (_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr);
      }

      ///
      /// <summary>
      /// Recomputes a rolling mean +/- stddev band into the internal scratch buffers if it has
      /// been invalidated since the last computation, windowed the same way as the centered
      /// moving average (every point whose x falls within movingSampleSize/2 of that point's own
      /// x), so the band moves with the data instead of being a single flat mean/stddev over the
      /// whole series. Because centering a point requires points that arrive after it, a point
      /// can only be finalized once enough later points actually exist; unless finalized is true,
      /// trailing points are left pending (NAN).
      /// </summary>
      ///
      void _recomputeStdDevBand()
      {
         if (!_stdDevDirty)
         {
            return;
         }

         if (_count == 0 || !_ensureStdDevBuffers(_count))
         {
            _stdDevReadyCount = 0;
            _stdDevDirty = false;
            return;
         }

         float halfWindow = movingSampleSize / 2.0f;

         size_t lo = 0;
         size_t hi = 0;
         float sum = 0.0f;
         float sumSquares = 0.0f;
         size_t finiteCount = 0;
         size_t readyCount = 0;

         for (size_t i = 0; i < _count; i++)
         {
            // Evict points that have aged out of the trailing (past) edge of the window.
            while (lo < _count && (_x[i] - _x[lo]) > halfWindow)
            {
               if (isfinite(_y[lo]))
               {
                  sum -= _y[lo];
                  sumSquares -= _y[lo] * _y[lo];
                  finiteCount--;
               }
               lo++;
            }

            // Fold in points up to the leading (future) edge of the window.
            while (hi < _count && (_x[hi] - _x[i]) <= halfWindow)
            {
               if (isfinite(_y[hi]))
               {
                  sum += _y[hi];
                  sumSquares += _y[hi] * _y[hi];
                  finiteCount++;
               }
               hi++;
            }

            // The window is only known to be fully populated once a point beyond its future
            // edge has actually been seen (hi < _count); otherwise more data may still arrive
            // that belongs inside it, unless the series has been marked finalized.
            if (!finalized && (hi >= _count))
            {
               break;
            }

            if (finiteCount > 0)
            {
               const float mean = sum / static_cast<float>(finiteCount);
               const float meanOfSquares = sumSquares / static_cast<float>(finiteCount);
               const float variance = meanOfSquares - (mean * mean);
               const float stdDev = sqrtf((variance > 0.0f) ? variance : 0.0f);
               _stdDevLowBuffer[i] = mean - stdDev;
               _stdDevHighBuffer[i] = mean + stdDev;
            }
            else
            {
               _stdDevLowBuffer[i] = NAN;
               _stdDevHighBuffer[i] = NAN;
            }

            readyCount = i + 1;
         }

         for (size_t i = readyCount; i < _count; i++)
         {
            _stdDevLowBuffer[i] = NAN;
            _stdDevHighBuffer[i] = NAN;
         }

         _stdDevReadyCount = readyCount;
         _stdDevDirty = false;
      }

      public:
   ///
   /// <summary>Width of the centered moving-average window, in the same units as the x values (sample count or time period).</summary>
   ///
   float movingSampleSize = 0.0f;

   ///
   /// <summary>
   /// Set once no more points will be added to this series, so the moving average's
   /// trailing points are computed from whatever surrounding data actually exists instead
   /// of being left pending.
   /// </summary>
   ///
   bool finalized = false;

   ///
   /// <summary>
   /// Constructs a new ScatterPlotSeries with the specified initial capacity.
   /// </summary>
   /// <param name="capacity">Initial capacity for the data arrays.</param>
   ///
   explicit ScatterPlotSeries(size_t capacity = 10)
      : _capacity(capacity)
   {
      if (capacity > 0)
      {
         _x = new (std::nothrow) float[capacity];
         _y = new (std::nothrow) float[capacity];

         if (_x == nullptr || _y == nullptr)
         {
            Util::setHaltReason("OOM allocating points in ScatterPlotSeries");
            Util::reset();
         }
      }
   }

   ScatterPlotSeries(const ScatterPlotSeries&) = delete;
   ScatterPlotSeries& operator=(const ScatterPlotSeries&) = delete;

   ///
   /// <summary>
   /// Destructor that frees allocated memory.
   /// </summary>
   ///
   ~ScatterPlotSeries()
   {
      delete[] _x;
      delete[] _y;
      delete[] _movingAverageBuffer;
      delete[] _stdDevLowBuffer;
      delete[] _stdDevHighBuffer;
   }

   ///
   /// <summary>
   /// Adds a new point to the series, growing its storage if needed. x values are
   /// expected to be non-decreasing since the moving average relies on that ordering.
   /// </summary>
   /// <param name="x">X coordinate value.</param>
   /// <param name="y">Y coordinate value.</param>
   ///
   void add(float x, float y)
   {
      _nextX = x + 1.0f;

      if (_count >= _capacity)
      {
         size_t newCapacity = _capacity + (_capacity / 2) + 1;
         float* newX = new (std::nothrow) float[newCapacity];
         float* newY = new (std::nothrow) float[newCapacity];

         if (newX == nullptr || newY == nullptr)
         {
            delete[] newX;
            delete[] newY;
            Util::setHaltReason("OOM allocating points in ScatterPlotSeries");
            Util::reset();
            return;
         }

         if (_count > 0)
         {
            memcpy(newX, _x, _count * sizeof(float));
            memcpy(newY, _y, _count * sizeof(float));
         }

         delete[] _x;
         delete[] _y;
         _x = newX;
         _y = newY;
         _capacity = newCapacity;
      }

      _x[_count] = x;
      _y[_count] = y;
      _count++;
      _movingAverageDirty = true;
      _stdDevDirty = true;

      if (isfinite(x))
      {
         if (!isfinite(_xMin) || (x < _xMin)) _xMin = x;
         if (!isfinite(_xMax) || (x > _xMax)) _xMax = x;
      }

      if (isfinite(y))
      {
         if (!isfinite(_yMin) || (y < _yMin)) _yMin = y;
         if (!isfinite(_yMax) || (y > _yMax)) _yMax = y;

         if (!_statsDirty)
         {
            _sumY += y;
            _sumSquaresY += y * y;
            _finiteYCount++;
         }
      }
   }

   ///
   /// <summary>
   /// Adds a new point to the series using an auto-incrementing X value (starting at 1 for
   /// the first point), matching the single-value add() semantics of IScatterPlotSeries /
   /// TimedScatterPlotSeries. Prefer the explicit add(x, y) overload when the caller already
   /// tracks its own X coordinate.
   /// </summary>
   /// <param name="value">Y coordinate value.</param>
   ///
   void add(float value) override
   {
      add(_nextX, value);
   }

      ///
      /// <summary>
      /// Sets the value at a specific index. Since this can modify any existing point
      /// (potentially invalidating a previously-computed min/max), it forces a one-time full
      /// rescan of the incrementally-tracked X/Y range on the next call that needs it, rather
      /// than trying to maintain the range incrementally here.
      /// </summary>
      /// <param name="index">Index to set.</param>
      /// <param name="x">X coordinate value.</param>
      /// <param name="y">Y coordinate value.</param>
      ///
      void set(size_t index, float x, float y)
      {
         if (index < _count)
         {
                  _x[index] = x;
                  _y[index] = y;
                  _movingAverageDirty = true;
                  _stdDevDirty = true;
                  _statsDirty = true;
                  _rangeDirty = true;
               }
            }

   ///
   /// <summary>
   /// Removes every point from the series and resets its moving-average cache, so the
   /// series can be reused (e.g. for a new test run). Does not reset showPoints,
   /// showLines, showMovingAverage, color, or movingSampleSize.
   /// </summary>
   ///
   void clear() override
   {
      _count = 0;
      _movingAverageReadyCount = 0;
      _movingAverageDirty = true;
      _stdDevReadyCount = 0;
      _stdDevDirty = true;
      finalized = false;
      _nextX = 0.0f;
      _xMin = NAN;
      _xMax = NAN;
      _yMin = NAN;
      _yMax = NAN;
      _sumY = 0.0f;
      _sumSquaresY = 0.0f;
      _finiteYCount = 0;
      _statsDirty = false;
      _rangeDirty = false;
   }

   ///
   /// <summary>
   /// Gets the X value at the specified index.
   /// </summary>
   /// <param name="index">Index to retrieve.</param>
   /// <returns>X coordinate value at the index, or 0 if the index is out of range.</returns>
   ///
   float getX(size_t index) const
   {
      return (index < _count) ? _x[index] : 0.0f;
   }

   ///
   /// <summary>
   /// Gets the Y value at the specified index.
   /// </summary>
   /// <param name="index">Index to retrieve.</param>
   /// <returns>Y coordinate value at the index, or 0 if the index is out of range.</returns>
   ///
   float getY(size_t index) const
   {
      return (index < _count) ? _y[index] : 0.0f;
   }

   ///
   /// <summary>
   /// Gets the current number of points in the series.
   /// </summary>
   /// <returns>Number of points.</returns>
   ///
   size_t getCount() const { return _count; }

   ///
   /// <summary>
   /// Gets the raw X/Y range of this series' points, maintained incrementally by add() in
   /// O(1) so callers (e.g. ScatterPlot::_computeAxisRange()) don't need to rescan every
   /// point on every call. If set() was used since the range was last computed, this does
   /// one full O(n) rescan to re-establish it before returning.
   /// </summary>
   /// <param name="outXMin">Receives the minimum finite X value found, or NAN if none exists.</param>
   /// <param name="outXMax">Receives the maximum finite X value found, or NAN if none exists.</param>
   /// <param name="outYMin">Receives the minimum finite Y value found, or NAN if none exists.</param>
   /// <param name="outYMax">Receives the maximum finite Y value found, or NAN if none exists.</param>
   ///
   void getRawRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax)
   {
      if (_rangeDirty)
      {
         _xMin = NAN;
         _xMax = NAN;
         _yMin = NAN;
         _yMax = NAN;

         for (size_t i = 0; i < _count; i++)
         {
            float x = _x[i];
            if (isfinite(x))
            {
               if (!isfinite(_xMin) || (x < _xMin)) _xMin = x;
               if (!isfinite(_xMax) || (x > _xMax)) _xMax = x;
            }

            float y = _y[i];
            if (isfinite(y))
            {
               if (!isfinite(_yMin) || (y < _yMin)) _yMin = y;
               if (!isfinite(_yMax) || (y > _yMax)) _yMax = y;
            }
         }

         _rangeDirty = false;
      }

      *outXMin = _xMin;
      *outXMax = _xMax;
      *outYMin = _yMin;
      *outYMax = _yMax;
   }

   ///
   /// <summary>
   /// Gets how many leading points of the centered moving average are finalized (safe to
   /// use), recomputing it first if any points have been added or set since the last call.
   /// </summary>
   /// <returns>Number of finalized leading moving-average points.</returns>
   ///
   size_t getMovingAverageReadyCount()
   {
      _recomputeMovingAverage();
      return _movingAverageReadyCount;
   }

   ///
   /// <summary>
   /// Gets the centered moving-average value at the specified raw point index,
   /// recomputing it first if any points have been added or set since the last call.
   /// </summary>
   /// <param name="index">Raw point index (parallel to getX()/getY()).</param>
   /// <returns>The centered moving-average value, or NAN if not yet finalized or out of range.</returns>
   ///
   float getMovingAverageY(size_t index)
   {
      _recomputeMovingAverage();
      return (index < _movingAverageReadyCount) ? _movingAverageBuffer[index] : NAN;
   }

   ///
   /// <summary>
   /// Computes the peak (maximum) value of the series' centered moving average.
   /// </summary>
   /// <returns>The maximum finite moving-average value, or NAN if none exists.</returns>
   ///
   float findMovingAveragePeak()
   {
      size_t readyCount = getMovingAverageReadyCount();

      float peak = NAN;
      for (size_t i = 0; i < readyCount; i++)
      {
         float value = _movingAverageBuffer[i];
         if (isfinite(value) && (!isfinite(peak) || (value > peak)))
         {
            peak = value;
         }
      }
      return peak;
   }

   ///
   /// <summary>
   /// Computes the min/max range of the series' centered moving average.
   /// </summary>
   /// <param name="outMin">Receives the minimum finite value found, or NAN if none exists.</param>
   /// <param name="outMax">Receives the maximum finite value found, or NAN if none exists.</param>
   ///
   void movingAverageRange(float* outMin, float* outMax)
   {
      size_t readyCount = getMovingAverageReadyCount();

      float minValue = NAN;
      float maxValue = NAN;
      for (size_t i = 0; i < readyCount; i++)
      {
         float value = _movingAverageBuffer[i];
         if (!isfinite(value))
         {
            continue;
         }
         if (!isfinite(minValue) || (value < minValue)) minValue = value;
         if (!isfinite(maxValue) || (value > maxValue)) maxValue = value;
      }

      *outMin = minValue;
      *outMax = maxValue;
   }

             ///
             /// <summary>
             /// Computes the mean and population standard deviation of the series' raw Y values.
             /// Uses the running sum/sum-of-squares maintained incrementally by add() so this is
             /// O(1) in the common case; if set() was used since the stats were last computed,
             /// this does one full O(n) rescan to re-establish them first.
             /// </summary>
             /// <param name="outMean">Receives the mean of the finite Y values, or NAN if none exist.</param>
             /// <param name="outStdDev">Receives the standard deviation of the finite Y values, or NAN if none exist.</param>
             ///
             void stdDevRange(float* outMean, float* outStdDev)
             {
                if (_statsDirty)
                {
                   _sumY = 0.0f;
                   _sumSquaresY = 0.0f;
                   _finiteYCount = 0;

                   for (size_t i = 0; i < _count; i++)
                   {
                      if (isfinite(_y[i]))
                      {
                         _sumY += _y[i];
                         _sumSquaresY += _y[i] * _y[i];
                         _finiteYCount++;
                      }
                   }

                   _statsDirty = false;
                }

                if (_finiteYCount == 0)
                {
                   *outMean = NAN;
                   *outStdDev = NAN;
                   return;
                }

                const float mean = _sumY / static_cast<float>(_finiteYCount);
                const float meanOfSquares = _sumSquaresY / static_cast<float>(_finiteYCount);
                const float variance = meanOfSquares - (mean * mean);

                *outMean = mean;
                *outStdDev = sqrtf((variance > 0.0f) ? variance : 0.0f);
             }

             ///
             /// <summary>
             /// Gets how many leading points of the rolling stddev band are finalized (safe to
             /// use), recomputing it first if any points have been added or set since the last call.
             /// </summary>
             /// <returns>Number of finalized leading stddev-band points.</returns>
             ///
             size_t getStdDevReadyCount()
             {
                _recomputeStdDevBand();
                return _stdDevReadyCount;
             }

             ///
             /// <summary>
             /// Gets the rolling stddev band's low value (mean - stddev) at the specified raw
             /// point index, recomputing the band first if any points have been added or set
             /// since the last call.
             /// </summary>
             /// <param name="index">Raw point index (parallel to getX()/getY()).</param>
             /// <returns>The low value of the band, or NAN if not yet finalized or out of range.</returns>
             ///
             float getStdDevLowY(size_t index)
             {
                _recomputeStdDevBand();
                return (index < _stdDevReadyCount) ? _stdDevLowBuffer[index] : NAN;
             }

             ///
             /// <summary>
             /// Gets the rolling stddev band's high value (mean + stddev) at the specified raw
             /// point index, recomputing the band first if any points have been added or set
             /// since the last call.
             /// </summary>
             /// <param name="index">Raw point index (parallel to getX()/getY()).</param>
             /// <returns>The high value of the band, or NAN if not yet finalized or out of range.</returns>
             ///
             float getStdDevHighY(size_t index)
             {
                _recomputeStdDevBand();
                return (index < _stdDevReadyCount) ? _stdDevHighBuffer[index] : NAN;
             }

             ///
             /// <summary>
             /// Computes the min/max range of the series' rolling stddev band.
             /// </summary>
             /// <param name="outMin">Receives the minimum finite value found, or NAN if none exists.</param>
             /// <param name="outMax">Receives the maximum finite value found, or NAN if none exists.</param>
             ///
             void stdDevBandRange(float* outMin, float* outMax)
             {
                size_t readyCount = getStdDevReadyCount();

                float minValue = NAN;
                float maxValue = NAN;
                for (size_t i = 0; i < readyCount; i++)
                {
                   float low = _stdDevLowBuffer[i];
                   float high = _stdDevHighBuffer[i];
                   if (isfinite(low) && (!isfinite(minValue) || (low < minValue))) minValue = low;
                   if (isfinite(high) && (!isfinite(maxValue) || (high > maxValue))) maxValue = high;
                }

                *outMin = minValue;
                *outMax = maxValue;
             }
          };


///
/// <summary>
/// Renders one or more ScatterPlotSeries on the display as a shared-axis scatter plot.
/// Callers create series via createSeries(), populate them by calling add() as data
/// becomes available, and set each series' display flags (showPoints, showLines,
/// showMovingAverage) and color. Calling render() scans every owned series, automatically
/// computes the shared X/Y axis range to fit whatever is currently displayed, and draws
/// axis labels plus each series' points/lines/moving-average line. Every series/layer
/// rasterizes its current frame into one plot-wide shared DisplayBuffer, each stamping a
/// distinct layer index, and the whole buffer is diffed against the previous frame's in a
/// single pass (see DisplayBuffer::diffAndDraw()), drawing only the pixels that actually
/// changed color; unchanging pixels are never redrawn or flashed. A full clear and redraw
/// of the chart area only happens when the computed axis range changes or invalidate()
/// was called since the last render().
/// </summary>
///
class ScatterPlot : public IScatterPlot
{
private:
   static constexpr int16_t Y_AXIS_LABEL_GAP = 2;

   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _width;
   int16_t _height;
   int16_t _yAxisWidth = 50;

   // Chart geometry and shared axis range established by the most recent full redraw.
   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   float _axisXMin = 0.0f;
   float _axisXMax = 0.0f;
   float _axisYMin = 0.0f;
   float _axisYMax = 0.0f;
   bool _hasRenderedFrame = false;
   bool _forceFullRedraw = false;

   // Tracks whether the chart's pixel geometry (_chartLeft/_chartTop/_chartWidth/
   // _chartHeight) has ever been established and the outer plot rect physically painted.
   // Unlike _hasRenderedFrame (which clear()/no-data render() resets so the next frame
   // knows to wait for a fresh axis range), this stays true across clear() since clear()
   // already physically erases the rect itself - so the next render() doesn't need to
   // redundantly fillRect() the same area again just because a full redraw was forced.
   bool _geometryEstablished = false;

   // Plot-wide shared 4-bit layer buffer used to rasterize every series/layer each frame
   // and diff against the previous frame, so only pixels that actually changed are redrawn.
   DisplayBuffer _displayBuffer;

   // Series management
   ScatterPlotSeries** _series = nullptr;
   size_t _seriesCount = 0;
   size_t _seriesCapacity = 0;

   // Axis format customization
   Format _xAxisFormat = Format("##.##");
   Format _yAxisFormat = Format("##.##", Format::Alignment::RIGHT);

   // Optional initial Y-axis range: the plot never shrinks below this range, but will
   // grow beyond it if the data requires a wider range.
   float _initialYMin = NAN;
   float _initialYMax = NAN;

   // Optional centered title drawn above the chart; when set, the chart area is reduced
   // to make room for it.
   String _title;

   // Chart colors. _outerBackgroundColor covers the area outside the plotted-data region
   // (e.g. behind axis labels/title); _plotAreaBackgroundColor covers the plotted-data
   // region itself (drawn via the DisplayBuffer's background/layer-0 color).
   Color _outerBackgroundColor = Color::BLACK;
   Color _plotAreaBackgroundColor = Color::BLACK;
   Color _axisColor = Color::GRAY;
   Color _labelColor = Color::LABEL;

   ///
   /// <summary>
   /// Computes the shared X/Y axis range needed to fit whatever is currently displayed
   /// across every owned series: a series' raw points contribute to the range only if
   /// showPoints or showLines is set (via its incrementally-tracked getRawRange(), so this
   /// no longer rescans every point on every call), and its moving average/stddev band
   /// contribute only if showMovingAverage/showStdDevBand is set, mirroring exactly what
   /// render() will go on to draw.
   /// </summary>
   /// <param name="outXMin">Receives the minimum finite X value found, or NAN if none exists.</param>
   /// <param name="outXMax">Receives the maximum finite X value found, or NAN if none exists.</param>
   /// <param name="outYMin">Receives the minimum finite Y value found, or NAN if none exists.</param>
   /// <param name="outYMax">Receives the maximum finite Y value found, or NAN if none exists.</param>
   ///
   void _computeAxisRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax)
   {
      float xMin = NAN;
      float xMax = NAN;
      float yMin = NAN;
      float yMax = NAN;

      for (size_t s = 0; s < _seriesCount; s++)
      {
         ScatterPlotSeries* series = _series[s];
         bool includeRaw = series->showPoints || series->showLines;

         float seriesXMin;
         float seriesXMax;
         float seriesYMin;
         float seriesYMax;
         series->getRawRange(&seriesXMin, &seriesXMax, &seriesYMin, &seriesYMax);

         if (isfinite(seriesXMin) && (!isfinite(xMin) || (seriesXMin < xMin))) xMin = seriesXMin;
         if (isfinite(seriesXMax) && (!isfinite(xMax) || (seriesXMax > xMax))) xMax = seriesXMax;

         if (includeRaw)
         {
            if (isfinite(seriesYMin) && (!isfinite(yMin) || (seriesYMin < yMin))) yMin = seriesYMin;
            if (isfinite(seriesYMax) && (!isfinite(yMax) || (seriesYMax > yMax))) yMax = seriesYMax;
         }

         if (series->showMovingAverage)
         {
            float maMin;
            float maMax;
            series->movingAverageRange(&maMin, &maMax);
            if (isfinite(maMin) && (!isfinite(yMin) || (maMin < yMin))) yMin = maMin;
            if (isfinite(maMax) && (!isfinite(yMax) || (maMax > yMax))) yMax = maMax;
         }

            if (series->showStdDevBand)
            {
               float bandMin;
               float bandMax;
               series->stdDevBandRange(&bandMin, &bandMax);
               if (isfinite(bandMin) && (!isfinite(yMin) || (bandMin < yMin))) yMin = bandMin;
               if (isfinite(bandMax) && (!isfinite(yMax) || (bandMax > yMax))) yMax = bandMax;
            }
         }

      *outXMin = xMin;
      *outXMax = xMax;
      *outYMin = isfinite(_initialYMin) ? ((!isfinite(yMin) || (_initialYMin < yMin)) ? _initialYMin : yMin) : yMin;
      *outYMax = isfinite(_initialYMax) ? ((!isfinite(yMax) || (_initialYMax > yMax)) ? _initialYMax : yMax) : yMax;
   }

   ///
   /// <summary>
   /// Formats a Y-axis label value using the current Y-axis format. Used by both chart
   /// geometry computation and axis-label drawing so the reserved label column width
   /// always matches what is actually drawn.
   /// </summary>
   /// <param name="value">Y-axis value to format.</param>
   /// <returns>Formatted label text.</returns>
   ///
   String _formatYLabel(float value) const
   {
      return String(_yAxisFormat.toString(value).c_str());
   }

   ///
   /// <summary>
   /// Computes the chart's pixel geometry. The Y-axis label column is sized directly from
   /// the fixed output length of _yAxisFormat (length() * charW()) rather than the rendered
   /// width of the current min/max label text, so the column width - and therefore
   /// _chartLeft/_chartWidth - never shifts as the axis range changes. This lets render()
   /// rely entirely on DisplayBuffer's diff to erase stale points on an axis-range-only
   /// change, without needing a full physical clear to handle a shifting chart rectangle.
   /// </summary>
   ///
   void _computeChartGeometry()
   {
      uint16_t yLabelWidth = static_cast<uint16_t>(_yAxisFormat.length()) * _display->charW();
      _yAxisWidth = static_cast<int16_t>(yLabelWidth) + Y_AXIS_LABEL_GAP;

      int16_t axisLineHeight = _display->charH() + 2;
      int16_t titlePadding = _display->charH() / 4;
      int16_t titleHeight = _title.length() > 0 ? (_display->charH() + 2 * titlePadding) : 0;

      _chartLeft = _x + _yAxisWidth;
      _chartTop = _y + titleHeight;
      _chartWidth = _width - _yAxisWidth;
      _chartHeight = _height - axisLineHeight - titleHeight;
   }

   ///
   /// <summary>
   /// Draws the gray X/Y axis lines and the auto-formatted min/max axis labels for the
   /// current chart geometry and axis range.
   /// </summary>
   ///
   void _drawAxes() const
   {
      if (_title.length() > 0)
      {
         int16_t titleX = _chartLeft + (_chartWidth - static_cast<int16_t>(_display->textWidth(_title.c_str()))) / 2;
         int16_t titlePadding = _display->charH() / 4;
         _display->setCursor(max(_chartLeft, titleX), _y + titlePadding);
         _display->print(_title, _labelColor, _outerBackgroundColor);
      }

         _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, _axisColor);
         _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, _axisColor);

         // Physically clear the reserved Y-axis label column and the X-axis label row before
         // drawing the new min/max labels. Labels are right-aligned (Y) or left/right-aligned
         // (X) based on their own rendered text width, so a narrower new label wouldn't
         // otherwise overwrite every pixel a previously-drawn wider label occupied, leaving
         // stale digits on screen (e.g. after recreatePlot() swaps in a new source/plot).
         int16_t yLabelColumnX = _x;
         int16_t yLabelColumnWidth = _chartLeft - Y_AXIS_LABEL_GAP - _x;
         _display->fillRect(yLabelColumnX, _chartTop, yLabelColumnWidth, _display->charH(), _outerBackgroundColor);
         _display->fillRect(yLabelColumnX, _chartTop + _chartHeight - _display->charH(), yLabelColumnWidth, _display->charH(), _outerBackgroundColor);
         _display->fillRect(_chartLeft, _chartTop + _chartHeight + 1, _chartWidth, _display->charH(), _outerBackgroundColor);

         String maxLabel = _formatYLabel(_axisYMax);
         int16_t maxLabelX = _chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_display->textWidth(maxLabel.c_str()));
         _display->setCursor(maxLabelX, _chartTop);
         _display->print(maxLabel, _labelColor, _outerBackgroundColor);

         String minLabel = _formatYLabel(_axisYMin);
         int16_t minLabelX = _chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_display->textWidth(minLabel.c_str()));
         minLabelX = max(static_cast<int16_t>(0), minLabelX);
         int16_t minLabelY = _chartTop + _chartHeight - _display->charH();
         _display->setCursor(minLabelX, minLabelY);
         _display->print(minLabel, _labelColor, _outerBackgroundColor);

         String rangeLabel = _formatYLabel(_axisYMax - _axisYMin);
         int16_t rangeLabelX = _chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_display->textWidth(rangeLabel.c_str()));
         rangeLabelX = max(static_cast<int16_t>(0), rangeLabelX);
         int16_t rangeLabelY = _chartTop + (_chartHeight - _display->charH()) / 2;
         _display->fillRect(yLabelColumnX, rangeLabelY, yLabelColumnWidth, _display->charH(), _outerBackgroundColor);
         _display->setCursor(rangeLabelX, rangeLabelY);
         _display->print(rangeLabel, Color::GRAY, _outerBackgroundColor);

         String xMinLabel = String(_xAxisFormat.toString(_axisXMin).c_str());
         _display->setCursor(_chartLeft, _chartTop + _chartHeight + 1);
         _display->print(xMinLabel, _labelColor, _outerBackgroundColor);

         // Trim trailing padding so the max label's measured width matches its visible
         // content; otherwise invisible trailing padding would push the text left of the
         // true right edge, making it look left-aligned instead of flush-right.
         String xMaxLabel = String(_xAxisFormat.toString(_axisXMax).c_str());
         while ((xMaxLabel.length() > 0) && (xMaxLabel[xMaxLabel.length() - 1] == ' '))
         {
            xMaxLabel.remove(xMaxLabel.length() - 1);
         }
         int16_t xMaxLabelX = _chartLeft + _chartWidth - static_cast<int16_t>(_display->textWidth(xMaxLabel.c_str()));
         xMaxLabelX = max(_chartLeft, xMaxLabelX);
         _display->setCursor(xMaxLabelX, _chartTop + _chartHeight + 1);
         _display->print(xMaxLabel, _labelColor, _outerBackgroundColor);
      }

          ///
          /// <summary>
          /// Converts a data point to buffer-relative pixel coordinates (0,0 at the chart's
          /// top-left) using the current chart geometry and axis range, clamping the result to
          /// stay within the chart area.
          /// </summary>
          /// <param name="x">X value to convert.</param>
          /// <param name="y">Y value to convert.</param>
          /// <param name="outX">Receives the buffer-relative pixel X coordinate.</param>
          /// <param name="outY">Receives the buffer-relative pixel Y coordinate.</param>
          ///
          void _toPixel(float x, float y, int16_t& outX, int16_t& outY) const
          {
             float xSpan = _axisXMax - _axisXMin;
             float ySpan = _axisYMax - _axisYMin;

             // When the axis has zero span (e.g. every plotted value is identical), center
             // the data in the chart instead of mapping it to the axis-minimum edge.
             if (xSpan <= 0.0f)
             {
                outX = static_cast<int16_t>((_chartWidth - 1) / 2);
             }
             else
             {
                outX = static_cast<int16_t>(((x - _axisXMin) / xSpan) * (_chartWidth - 1));
             }

             // The bottom-most chart row is reserved for the gray X-axis line (drawn by
             // _drawAxes()), so data points are confined to chartHeight - 1 rows above it;
             // otherwise a minimum-value sample would land on the same row as the axis line
             // and overwrite it.
             if (ySpan <= 0.0f)
             {
                outY = static_cast<int16_t>((_chartHeight - 2) / 2);
             }
             else
             {
                outY = static_cast<int16_t>(((_axisYMax - y) / ySpan) * (_chartHeight - 2));
             }

             outX = constrain(outX, static_cast<int16_t>(0), static_cast<int16_t>(_chartWidth - 1));
             outY = constrain(outY, static_cast<int16_t>(0), static_cast<int16_t>(_chartHeight - 2));
          }

          ///
          /// <summary>
          /// Rasterizes either a series' raw points (connected with lines when showLines is
          /// set) or its moving-average line into the plot-wide shared display buffer under the
          /// given layer index. Does not clear the buffer first; callers rasterize every
          /// series/layer into the same shared buffer before diffing once.
          /// </summary>
          /// <param name="series">Series to rasterize.</param>
          /// <param name="useMovingAverage">If true, rasterizes the moving-average values (always connected) instead of the raw points.</param>
          /// <param name="layer">Layer index (1..DisplayBuffer::MAX_LAYERS) to stamp for this data.</param>
          ///
          void _rasterizeSeriesData(ScatterPlotSeries* series, bool useMovingAverage, uint8_t layer)
          {
             size_t count = useMovingAverage ? series->getMovingAverageReadyCount() : series->getCount();
             if (count == 0 || layer == 0)
             {
                return;
             }

             bool connectPoints = useMovingAverage || series->showLines;

             bool havePrevPoint = false;
             int16_t prevX = 0;
             int16_t prevY = 0;

             for (size_t i = 0; i < count; i++)
             {
                float value = useMovingAverage ? series->getMovingAverageY(i) : series->getY(i);
                if (!isfinite(value))
                {
                   continue;
                }

                int16_t x;
                int16_t y;
                _toPixel(series->getX(i), value, x, y);

                if (connectPoints && havePrevPoint)
                {
                   _displayBuffer.drawLine(prevX, prevY, x, y, layer);
                }
                else
                {
                   _displayBuffer.setPixel(x, y, layer);
                }

                      prevX = x;
                      prevY = y;
                      havePrevPoint = true;
                   }
                }

                ///
                /// <summary>
                /// Rasterizes a series' rolling mean +/- stddev band (low and high edges, each
                /// connected with lines) into the plot-wide shared display buffer under the given
                /// layer index. Does not clear the buffer first; callers rasterize every
                /// series/layer into the same shared buffer before diffing once.
                /// </summary>
                /// <param name="series">Series to rasterize.</param>
                /// <param name="layer">Layer index (1..DisplayBuffer::MAX_LAYERS) to stamp for this data.</param>
                ///
                void _rasterizeStdDevBand(ScatterPlotSeries* series, uint8_t layer)
                {
                   size_t count = series->getStdDevReadyCount();
                   if (count == 0 || layer == 0)
                   {
                      return;
                   }

                   bool haveLowPrev = false;
                   int16_t lowPrevX = 0;
                   int16_t lowPrevY = 0;
                   bool haveHighPrev = false;
                   int16_t highPrevX = 0;
                   int16_t highPrevY = 0;

                   for (size_t i = 0; i < count; i++)
                   {
                      float x = series->getX(i);

                      float low = series->getStdDevLowY(i);
                      if (isfinite(low))
                      {
                         int16_t px;
                         int16_t py;
                         _toPixel(x, low, px, py);
                         if (haveLowPrev)
                         {
                            _displayBuffer.drawLine(lowPrevX, lowPrevY, px, py, layer);
                         }
                         else
                         {
                            _displayBuffer.setPixel(px, py, layer);
                         }
                         lowPrevX = px;
                         lowPrevY = py;
                         haveLowPrev = true;
                      }
                      else
                      {
                         haveLowPrev = false;
                      }

                      float high = series->getStdDevHighY(i);
                      if (isfinite(high))
                      {
                         int16_t px;
                         int16_t py;
                         _toPixel(x, high, px, py);
                         if (haveHighPrev)
                         {
                            _displayBuffer.drawLine(highPrevX, highPrevY, px, py, layer);
                         }
                         else
                         {
                            _displayBuffer.setPixel(px, py, layer);
                         }
                         highPrevX = px;
                         highPrevY = py;
                         haveHighPrev = true;
                      }
                      else
                      {
                         haveHighPrev = false;
                      }
                   }
                }

       public:
          ///
          /// <summary>
          /// Initializes a new ScatterPlot at the specified position and dimensions.
          /// </summary>
          /// <param name="display">Pointer to the display object.</param>
          /// <param name="x">X coordinate for the plot.</param>
          /// <param name="y">Y coordinate for the plot.</param>
          /// <param name="width">Width of the plot area in pixels.</param>
          /// <param name="height">Height of the plot area in pixels.</param>
          /// <param name="title">Optional centered title drawn above the chart; defaults to none, in which case the plot uses the full area.</param>
          ///
          ScatterPlot(ArduinoWithDisplay* display, int16_t x, int16_t y, int16_t width, int16_t height, const String& title = String())
             : _display(display), _x(x), _y(y), _width(width), _height(height), _title(title)
          {
          }

          ///
          /// <summary>
          /// Initializes a new ScatterPlot within the given fixed screen rectangle, matching the
          /// Rect16-based constructor shape of TimedScatterPlot (see IScatterPlot).
          /// </summary>
          /// <param name="display">Pointer to the display object.</param>
          /// <param name="rect">Fixed screen rectangle this plot draws within.</param>
          /// <param name="title">Optional centered title drawn above the chart; defaults to none, in which case the plot uses the full area.</param>
          ///
          ScatterPlot(ArduinoWithDisplay* display, Rect16 rect, const String& title = String())
             : ScatterPlot(display, static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
                static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height), title)
          {
          }

   ~ScatterPlot()
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
      }
      delete[] _series;
   }

   ///
   /// <summary>
   /// Repositions and/or resizes the plot area. Forces a full redraw on the next
   /// render() since the chart geometry changes.
   /// </summary>
   /// <param name="x">X coordinate for the plot.</param>
   /// <param name="y">Y coordinate for the plot.</param>
   /// <param name="width">Width of the plot area in pixels.</param>
   /// <param name="height">Height of the plot area in pixels.</param>
   ///
   void setRect(int16_t x, int16_t y, int16_t width, int16_t height)
   {
      _x = x;
      _y = y;
      _width = width;
      _height = height;
      _forceFullRedraw = true;
      _geometryEstablished = false;
   }

   ///
   /// <summary>
   /// Creates a new scatter plot series owned by this plot, with the specified initial
   /// capacity.
   /// </summary>
   /// <param name="capacity">Initial capacity for the series' data arrays.</param>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   ScatterPlotSeries* createSeries(size_t capacity)
   {
      if (_seriesCount >= _seriesCapacity)
      {
         size_t newCapacity = _seriesCapacity + 5;
         ScatterPlotSeries** newSeries = new (std::nothrow) ScatterPlotSeries*[newCapacity];

         if (newSeries == nullptr)
         {
            Util::setHaltReason("OOM allocating series array in ScatterPlot");
            Util::reset();
            return nullptr;
         }

         if (_seriesCount > 0)
         {
            memcpy(newSeries, _series, _seriesCount * sizeof(ScatterPlotSeries*));
         }

         delete[] _series;
         _series = newSeries;
         _seriesCapacity = newCapacity;
      }

      ScatterPlotSeries* newSeries = new ScatterPlotSeries(capacity);
      _series[_seriesCount++] = newSeries;
      return newSeries;
   }

   ///
   /// <summary>
   /// Creates a new scatter plot series owned by this plot, using a default initial
   /// capacity of 10 (see IScatterPlot::createSeries()).
   /// </summary>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   IScatterPlotSeries* createSeries() override
   {
      return createSeries(10);
   }

   ///
   /// <summary>
   /// Gets a series by index.
   /// </summary>
   /// <param name="index">Index of the series to retrieve.</param>
   /// <returns>Pointer to the series at the specified index, or nullptr if index is invalid.</returns>
   ///
   IScatterPlotSeries* getSeries(size_t index) override
   {
      return (index < _seriesCount) ? _series[index] : nullptr;
   }

   ///
   /// <summary>
   /// Gets the number of series currently managed by this plot.
   /// </summary>
   /// <returns>Number of series.</returns>
   ///
   size_t getSeriesCount() const override
   {
      return _seriesCount;
   }

   ///
   /// <summary>
   /// Deletes a series at the specified index.
   /// </summary>
   /// <param name="index">Index of the series to delete.</param>
   ///
   void deleteSeries(size_t index) override
   {
      if (index < _seriesCount)
      {
         delete _series[index];

         for (size_t i = index; i < _seriesCount - 1; i++)
         {
            _series[i] = _series[i + 1];
         }

         _seriesCount--;
      }
   }

   ///
   /// <summary>
   /// Deletes all series managed by this plot.
   /// </summary>
   ///
   void deleteAllSeries() override
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
      }
      _seriesCount = 0;
   }

   ///
   /// <summary>
   /// Forces the next render() call to perform a full clear and redraw of the chart area,
   /// even if the computed axis range has not changed. Use this after externally clearing
   /// the display or otherwise invalidating pixels render() would not normally redraw.
   /// </summary>
   ///
   void invalidate() override
   {
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Immediately and physically erases any previously rendered chart content (axes, points,
   /// lines, etc.) and forces the next render() to perform a full redraw. Unlike invalidate(),
   /// which only takes effect the next time render() is called with plottable data, this acts
   /// right away - use this when clearing a series (e.g. switching data sources) so stale
   /// content doesn't linger on screen until enough new samples accumulate to make
   /// render()'s computed axis range valid again.
   /// </summary>
   ///
   void clear() override
   {
      bool alreadyErased = (_display != nullptr && _hasRenderedFrame);
      if (alreadyErased)
      {
         _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);
      }

      _displayBuffer.reset(alreadyErased);

      _hasRenderedFrame = false;
      _forceFullRedraw = true;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         _series[i]->clear();
      }
   }

   ///
   /// <summary>
   /// Sets or clears the centered title drawn above the chart. Pass an empty string to
   /// remove the title, restoring the full plot area to the chart. Forces a full redraw
   /// on the next render() since the chart geometry changes.
   /// </summary>
   /// <param name="title">Title text to display, or an empty string for none.</param>
   ///
   void setTitle(const String& title)
   {
      _title = title;
      _forceFullRedraw = true;
      _geometryEstablished = false;
   }

   ///
   /// <summary>
   /// Gets the current title text, or an empty string if none is set.
   /// </summary>
   /// <returns>Current title text.</returns>
   ///
   const String& getTitle() const
   {
      return _title;
   }

   ///
   /// <summary>
   /// Sets the format used to render X-axis min/max labels.
   /// </summary>
   /// <param name="format">Format to apply to the X-axis min/max labels.</param>
   ///
   void setXAxisFormat(const Format& format)
   {
      _xAxisFormat = format;
   }

   ///
   /// <summary>
   /// Sets the format used to render Y-axis min/max labels. Forces a full redraw on the
   /// next render() since the reserved Y-axis label column width is derived from this
   /// format's fixed length.
   /// </summary>
   /// <param name="format">Format to apply to the Y-axis min/max labels.</param>
   ///
   void setYAxisFormat(const Format& format) override
   {
      // Y-axis labels are always drawn flush against the chart's left edge, so force
      // right alignment on our own copy regardless of how the caller's format is aligned
      // (e.g. a sensor's format may be left-aligned for use elsewhere, like a table).
         _yAxisFormat = format.clone(Format::Alignment::RIGHT);
         _forceFullRedraw = true;
         _geometryEstablished = false;
      }

   ///
   /// <summary>
   /// Sets an initial Y-axis range the chart starts with before any data requires a wider
   /// range. The plot's Y range never shrinks below [min, max], but grows beyond it as
   /// needed to fit newly added data. Pass NAN for either value to disable that bound.
   /// </summary>
   /// <param name="min">Initial minimum Y value.</param>
   /// <param name="max">Initial maximum Y value.</param>
   ///
   void setInitialYRange(float min, float max)
   {
      _initialYMin = min;
      _initialYMax = max;
   }

   ///
   /// <summary>
   /// Sets the colors used to draw the chart's non-data elements. Forces a full redraw on
   /// the next render() since the plot-area background is repainted immediately.
   /// </summary>
   /// <param name="outerBackgroundColor">Color used outside the plotted-data area (e.g. behind axis labels/title).</param>
   /// <param name="plotAreaBackgroundColor">Color used behind the plotted-data area itself.</param>
   /// <param name="axisColor">Color used to draw the axis lines.</param>
   /// <param name="labelColor">Color used to draw axis labels and the title.</param>
   ///
   void setColors(Color outerBackgroundColor, Color plotAreaBackgroundColor, Color axisColor, Color labelColor) override
   {
      _outerBackgroundColor = outerBackgroundColor;
      _plotAreaBackgroundColor = plotAreaBackgroundColor;
      _axisColor = axisColor;
      _labelColor = labelColor;
      _forceFullRedraw = true;
      _geometryEstablished = false;
   }

   ///
   /// <summary>
   /// Gets the left pixel coordinate of the chart's plotted-data area (after the reserved
   /// Y-axis label column), as computed by the most recent render(). Useful for aligning
   /// another chart's x-axis (e.g. a HistogramPlot) with this one.
   /// </summary>
   /// <returns>Left pixel coordinate of the chart area.</returns>
   ///
   int16_t getChartLeft() const
   {
      return _chartLeft;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the chart's plotted-data area (excluding the reserved
   /// Y-axis label column), as computed by the most recent render(). Useful for aligning
   /// another chart's x-axis (e.g. a HistogramPlot) with this one.
   /// </summary>
   /// <returns>Width of the chart area, in pixels.</returns>
   ///
   int16_t getChartWidth() const
   {
      return _chartWidth;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the reserved Y-axis label column, as computed by the most
   /// recent render(). Useful for reserving a matching column in another chart (e.g. a
   /// HistogramPlot) so both charts' x-axes line up.
   /// </summary>
   /// <returns>Width of the reserved Y-axis label column, in pixels.</returns>
   ///
   int16_t getYAxisWidth() const
   {
      return _yAxisWidth;
   }

       ///
       /// <summary>
       /// Renders every owned series: automatically computes the shared X/Y axis range to fit
       /// whatever is currently displayed (based on each series' showPoints/showLines/
       /// showMovingAverage flags), then draws axis labels and each series' points, lines,
       /// and/or moving-average line. Every series/layer rasterizes its current frame into one
       /// plot-wide shared DisplayBuffer, each stamping a distinct layer index, and the whole
       /// buffer is diffed against the previous frame's in a single pass (see
       /// DisplayBuffer::diffAndDraw()), drawing only the pixels that actually changed color;
       /// unchanging pixels are never redrawn or flashed, and stale points from a prior axis
       /// range are naturally erased by the diff rather than a full clear. Axis labels and the
       /// chart geometry are only recomputed/redrawn when the computed axis range changes or
       /// invalidate() was called since the last render().
       /// </summary>
       ///
          void render() override
          {
             if (_display == nullptr || _seriesCount == 0)
             {
                return;
             }

             float xMin;
             float xMax;
             float yMin;
             float yMax;
             _computeAxisRange(&xMin, &xMax, &yMin, &yMax);

             if (!isfinite(xMin) || !isfinite(xMax) || !isfinite(yMin) || !isfinite(yMax))
             {
                // No plottable data right now (e.g. every series was just cleared for a new
                // source). Physically erase any previously rendered chart content instead of
                // silently leaving stale points/axes on screen, then wait for real data before
                // establishing a new axis range.
                if (_hasRenderedFrame)
                {
                   _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);
                   _displayBuffer.reset(/* alreadyPhysicallyErased */ true);
                   _hasRenderedFrame = false;
                   _forceFullRedraw = true;
                }
                return;
             }

             bool axisChanged = !_hasRenderedFrame || (xMin != _axisXMin) || (xMax != _axisXMax) || (yMin != _axisYMin) || (yMax != _axisYMax);
             bool needsFullRedraw = _forceFullRedraw || axisChanged;

             _display->setTextSize(2);

             // Tracks whether the chart's physical rect was (re)painted this frame (e.g.
             // setRect()/title change, or the very first frame for a brand-new plot
             // instance); declared outside the needsFullRedraw block below since it's also
             // needed afterward to keep the display buffer's previous-frame state in sync
             // with the physical display.
             bool geometryChanged = false;

             if (needsFullRedraw)
             {
                const int16_t oldChartLeft = _chartLeft;
                const int16_t oldChartTop = _chartTop;
                const int16_t oldChartWidth = _chartWidth;
                const int16_t oldChartHeight = _chartHeight;

                _computeChartGeometry();

                if (_chartWidth < 20 || _chartHeight < 20)
                {
                   return;
                }

                // Only repaint the full plot rect (title, axis-label gutter, and chart
                // interior) when the chart geometry actually changed (e.g. setRect()/title
                // change) or it hasn't been physically painted yet; an axis-range-only change
                // (which happens on most frames with live/noisy data) keeps the same geometry,
                // so repainting the whole rect every time would flicker the axis-label margins
                // for no reason - the chart interior is still fully refreshed below via
                // _displayBuffer's diffAndDraw(). Note this intentionally checks
                // _geometryEstablished rather than _hasRenderedFrame: clear() already
                // physically erases the rect itself and leaves _geometryEstablished true, so
                // the next render() doesn't redundantly fillRect() the same area again.
                geometryChanged = !_geometryEstablished
                   || (_chartLeft != oldChartLeft) || (_chartTop != oldChartTop)
                   || (_chartWidth != oldChartWidth) || (_chartHeight != oldChartHeight);

                if (geometryChanged)
                {
                   _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);
                }

                _geometryEstablished = true;

                // No physical black flash-clear of the chart interior is needed here: it is
                // rendered entirely through _displayBuffer below, whose diffAndDraw() already
                // erases (draws black over) any pixel that was lit last frame but isn't lit
                // this frame - including every stale point invalidated by the new axis range.

                _axisXMin = xMin;
                _axisXMax = xMax;
                _axisYMin = yMin;
                _axisYMax = yMax;
                _forceFullRedraw = false;

                _drawAxes();
             }
             else if (_chartWidth < 20 || _chartHeight < 20)
             {
                return;
             }

             // The plot-wide display buffer is sized to the current chart dimensions; bind it
             // before diffing occurs. bind() only reallocates (and clears) its buffers when the
             // chart's pixel dimensions actually change; an axis-range-only change reuses the
             // existing buffer so diffAndDraw() can still detect and erase stale points from the
             // previous axis mapping, without any separate full clear/reset here.
             _displayBuffer.bind(_display, _chartLeft, _chartTop, _chartWidth, _chartHeight);
             _displayBuffer.setBackgroundColor(_plotAreaBackgroundColor);

             // Whenever geometryChanged just physically repainted the chart interior via the
             // fillRect() above (e.g. a brand-new plot instance created by recreatePlot() when
             // switching Source), the display buffer's previous-frame state must be told that
             // the area was already erased. Otherwise bind() (whether it just (re)allocated
             // fresh buffers primed to "unknown/changed", or is reusing buffers from a
             // differently-sized previous plot) leaves _prevMask out of sync with what's
             // actually on screen, causing diffAndDraw() below to individually redraw every
             // single pixel in the chart interior to "correct" a mismatch that doesn't really
             // exist - the slow, visible left-to-right clear the user saw on a Source change.
             if (geometryChanged)
             {
                _displayBuffer.reset(/* alreadyPhysicallyErased */ true);
             }

             _displayBuffer.clear();

             // Assign each series/layer (raw, moving-average, stddev band) the next free nibble
             // layer index and record its color in the display buffer's palette, then rasterize
             // it into the one shared buffer. Layer 0 is reserved for "unlit"; only
             // DisplayBuffer::MAX_LAYERS distinct layers can be drawn in a single frame given the
             // 4-bit buffer width. Raw points are rasterized first for every series, then moving
             // averages, then stddev bands, so overlays are always drawn on top of raw points
             // (and stddev bands on top of moving averages) even when multiple series overlap.
             uint8_t nextLayer = 1;

             for (size_t i = 0; i < _seriesCount; i++)
             {
                ScatterPlotSeries* series = _series[i];

                if ((series->showPoints || series->showLines) && nextLayer <= DisplayBuffer::MAX_LAYERS)
                {
                   uint8_t layer = nextLayer++;
                   _displayBuffer.setPaletteColor(layer, series->color);
                   _rasterizeSeriesData(series, false, layer);
                }
             }

             for (size_t i = 0; i < _seriesCount; i++)
             {
                ScatterPlotSeries* series = _series[i];

                if (series->showMovingAverage && nextLayer <= DisplayBuffer::MAX_LAYERS)
                {
                   uint8_t layer = nextLayer++;
                   _displayBuffer.setPaletteColor(layer, series->movingAverageColor);
                   _rasterizeSeriesData(series, true, layer);
                }
             }

             for (size_t i = 0; i < _seriesCount; i++)
             {
                ScatterPlotSeries* series = _series[i];

                if (series->showStdDevBand && nextLayer <= DisplayBuffer::MAX_LAYERS)
                {
                   uint8_t layer = nextLayer++;
                   _displayBuffer.setPaletteColor(layer, series->stdDevBandColor);
                   _rasterizeStdDevBand(series, layer);
                }
             }

             _displayBuffer.diffAndDraw();

             _hasRenderedFrame = true;
          }
       };
