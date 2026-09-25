#pragma once

#include "ColorX.h"
#include "IScatterPlotSeries.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// Common base for every data series owned and rendered by a ScatterPlot: stores its own
/// (x, y) points, display flags (points, connected lines, moving-average line), and color
/// (all inherited from IScatterPlotSeries). Also owns a centered moving-average and a
/// rolling mean +/- stddev band of its own points, both computed on demand from
/// movingSampleSize (in the same units as the x values) and cached until new points are
/// added. Set finalized once no more points will be added so the moving average's trailing
/// samples (which need future points to center on) are computed from whatever data
/// actually exists instead of being left pending.
///
/// Concrete series types (ScatterPlotSeries, TimedScatterPlotSeries) differ only in how
/// they store incoming samples - a plain growing array, a fixed-range/rolling-count
/// buffer, or a rotating time-bin history - which they implement via add()/clear() and the
/// _refreshStorage() hook below; every other accessor (range tracking, moving average,
/// stddev band, render-facing buffers) is shared here.
/// </summary>
///
class ScatterPlotSeriesBase : public IScatterPlotSeries
{
   friend class ScatterPlot;

protected:
   float* _x = nullptr;
   float* _y = nullptr;
   size_t _count = 0;
   size_t _capacity = 0;

   float _nextX = 0.0f;

   float _xMin = NAN;
   float _xMax = NAN;
   float _yMin = NAN;
   float _yMax = NAN;

   float _sumY = 0.0f;
   float _sumSquaresY = 0.0f;
   size_t _finiteYCount = 0;
   bool _statsDirty = false;

   bool _rangeDirty = false;

   bool _hasFixedXRange = false;
   float _fixedXMin = NAN;
   float _fixedXMax = NAN;

   float* _movingAverageBuffer = nullptr;
   size_t _movingAverageBufferCapacity = 0;
   size_t _movingAverageReadyCount = 0;
   bool _movingAverageDirty = true;

   // Cached sliding-window state as of the last successfully finalized (ready) index, so
   // a subsequent recompute can resume the two-pointer scan from _movingAverageReadyCount
   // instead of rescanning from index 0 - already-ready values never change once computed.
   size_t _movingAverageLo = 0;
   size_t _movingAverageHi = 0;
   float _movingAverageSum = 0.0f;
   size_t _movingAverageFiniteCount = 0;

   float* _stdDevLowBuffer = nullptr;
   float* _stdDevHighBuffer = nullptr;
   size_t _stdDevBufferCapacity = 0;
   size_t _stdDevReadyCount = 0;
   bool _stdDevDirty = true;

    // Cached sliding-window state mirroring _movingAverageLo/Hi/Sum/FiniteCount above, but
   // for the stddev band's scan (which also needs a running sum of squares). These are
   // kept as double (rather than float, like the moving-average accumulators) because the
   // variance formula below (E[x^2] - E[x]^2) subtracts two values that are both on the
   // order of the squared sample magnitude, even when the actual variance is tiny; at
   // float precision that subtraction suffers catastrophic cancellation and can go
   // slightly negative (getting clamped to a false zero stddev) for real, noisy data.
   size_t _stdDevLo = 0;
   size_t _stdDevHi = 0;
   double _stdDevSum = 0.0;
   double _stdDevSumSquares = 0.0;
   size_t _stdDevFiniteCount = 0;

   // Highest X value of an actual submitted sample (as opposed to _xMax, which in
   // fixed-range bin mode reflects the full locked axis range's last bin center even
   // before real data has arrived there). Used to gate moving-average/stddev readiness
   // so a window whose right edge extends past real data is never reported as ready,
   // even though bin-mode's _count is the full, fixed bin count from the first add().
   float _maxSampleX = NAN;

   virtual void _refreshStorage()
   {
   }

   virtual void _clearDerivedState()
   {
   }

   void _appendRaw(float x, float y)
   {
      if (_count >= _capacity)
      {
         size_t newCapacity = _capacity + (_capacity / 2) + 1;
         float* newX = new (std::nothrow) float[newCapacity];
         float* newY = new (std::nothrow) float[newCapacity];

         if (newX == nullptr || newY == nullptr)
         {
            delete[] newX;
            delete[] newY;
            Util::reset(0.0f, "OOM allocating points in ScatterPlotSeries");
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
         if (!isfinite(_maxSampleX) || (x > _maxSampleX)) _maxSampleX = x;
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

   bool _ensureMovingAverageBuffer(size_t count)
   {
      if (count <= _movingAverageBufferCapacity)
      {
         return _movingAverageBuffer != nullptr;
      }

      float* newBuffer = new (std::nothrow) float[count];
      if (newBuffer == nullptr)
      {
         return false;
      }

      // Preserve already-computed values so a growing buffer doesn't lose the work an
      // incremental recompute has already done at lower indices.
      if (_movingAverageBuffer != nullptr && _movingAverageBufferCapacity > 0)
      {
         memcpy(newBuffer, _movingAverageBuffer, _movingAverageBufferCapacity * sizeof(float));
      }

      delete[] _movingAverageBuffer;
      _movingAverageBuffer = newBuffer;
      _movingAverageBufferCapacity = count;

      return true;
   }

   void _recomputeMovingAverage()
   {
      _refreshStorage();

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

      // Resume the sliding-window scan from the last already-finalized index instead of
      // rescanning from 0: every index below _movingAverageReadyCount was already computed
      // from a window that can never change (its data only depends on samples at or before
      // it, which are immutable once appended), so re-deriving it would be wasted work.
      size_t lo = _movingAverageLo;
      size_t hi = _movingAverageHi;
      float sum = _movingAverageSum;
      size_t finiteCount = _movingAverageFiniteCount;
      size_t readyCount = _movingAverageReadyCount;

      for (size_t i = readyCount; i < _count; i++)
      {
         while (lo < _count && (_x[i] - _x[lo]) > halfWindow)
         {
            if (isfinite(_y[lo]))
            {
               sum -= _y[lo];
               finiteCount--;
            }
            lo++;
         }

         while (hi < _count && (_x[hi] - _x[i]) <= halfWindow)
         {
            if (isfinite(_y[hi]))
            {
               sum += _y[hi];
               finiteCount++;
            }
            hi++;
         }

         if (!finalized && (!isfinite(_maxSampleX) || ((_x[i] + halfWindow) > _maxSampleX)))
         {
            break;
         }

         // Only emit a moving-average vertex at positions that actually received a real
         // raw sample (isfinite(_y[i])); positions that never got a real sample (e.g.
         // an empty bin in fixed-range/timed bin mode) get NAN here so the renderer skips
         // them and draws a straight, linearly-interpolated line to the next real vertex
         // instead of a flat, staircase-like segment through an empty position.
         _movingAverageBuffer[i] = (isfinite(_y[i]) && (finiteCount > 0)) ? (sum / finiteCount) : NAN;
         readyCount = i + 1;

         // Persist the window state as of this now-finalized index so the next call can
         // resume from here rather than rescanning.
         _movingAverageLo = lo;
         _movingAverageHi = hi;
         _movingAverageSum = sum;
         _movingAverageFiniteCount = finiteCount;
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
   /// Shifts the cached moving-average and stddev-band buffers left by one slot to mirror
   /// a scrolling series (rolling-count or time-bin rotation) dropping its oldest raw
   /// sample: since those buffers are indexed by fixed slot position and the underlying
   /// window shape is invariant under a uniform shift, the value previously computed for
   /// slot i + 1 is still correct for slot i, so it is reused instead of being recomputed.
   /// The cached sliding-window scan state (lo/hi/sum/etc., used to resume an incremental
   /// recompute) is adjusted in lockstep so the next recompute only has to fill in the
   /// newly exposed trailing slot instead of rescanning everything.
   /// </summary>
   /// <param name="currentCount">Number of valid slots in the scrolling buffer (its fixed capacity).</param>
   /// <param name="droppedY">The raw Y value being dropped from slot 0, needed to remove its contribution from the cached window sums if it was still included in them.</param>
   ///
   void _rollOverlaysLeft(size_t currentCount, float droppedY)
   {
      if (currentCount == 0)
      {
         return;
      }

      bool hasMovingAvg = (_movingAverageBuffer != nullptr) && (currentCount <= _movingAverageBufferCapacity);
      bool hasStdDev = (_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr) && (currentCount <= _stdDevBufferCapacity);

      if (hasMovingAvg)
      {
         if (currentCount > 1)
         {
            memmove(_movingAverageBuffer, _movingAverageBuffer + 1, (currentCount - 1) * sizeof(float));
         }
         if (_movingAverageReadyCount > 0)
         {
            _movingAverageReadyCount--;
         }

         // Remove the dropped sample's contribution if the cached window had not yet
         // excluded it, then shift the window pointers down to match the new indices.
         if (_movingAverageLo == 0)
         {
            if (isfinite(droppedY))
            {
               _movingAverageSum -= droppedY;
               _movingAverageFiniteCount--;
            }
         }
         else
         {
            _movingAverageLo--;
         }

         if (_movingAverageHi > 0)
         {
            _movingAverageHi--;
         }

         // The newly exposed trailing slot has never been computed; request a recompute,
         // which (thanks to the resumed scan above) will only need to fill in this slot.
         _movingAverageBuffer[currentCount - 1] = NAN;
         _movingAverageDirty = true;
      }

      if (hasStdDev)
      {
         if (currentCount > 1)
         {
            memmove(_stdDevLowBuffer, _stdDevLowBuffer + 1, (currentCount - 1) * sizeof(float));
            memmove(_stdDevHighBuffer, _stdDevHighBuffer + 1, (currentCount - 1) * sizeof(float));
         }
         if (_stdDevReadyCount > 0)
         {
            _stdDevReadyCount--;
         }

         if (_stdDevLo == 0)
         {
            if (isfinite(droppedY))
            {
               _stdDevSum -= droppedY;
               _stdDevSumSquares -= static_cast<double>(droppedY) * droppedY;
               _stdDevFiniteCount--;
            }
         }
         else
         {
            _stdDevLo--;
         }

         if (_stdDevHi > 0)
         {
            _stdDevHi--;
         }

         _stdDevLowBuffer[currentCount - 1] = NAN;
         _stdDevHighBuffer[currentCount - 1] = NAN;
         _stdDevDirty = true;
      }
   }

   bool _ensureStdDevBuffers(size_t count)
   {
      if (count <= _stdDevBufferCapacity)
      {
         return (_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr);
      }

      float* newLowBuffer = new (std::nothrow) float[count];
      float* newHighBuffer = new (std::nothrow) float[count];
      if (newLowBuffer == nullptr || newHighBuffer == nullptr)
      {
         delete[] newLowBuffer;
         delete[] newHighBuffer;
         return false;
      }

      // Preserve already-computed values so a growing buffer doesn't lose the work an
      // incremental recompute has already done at lower indices.
      if ((_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr) && (_stdDevBufferCapacity > 0))
      {
         memcpy(newLowBuffer, _stdDevLowBuffer, _stdDevBufferCapacity * sizeof(float));
         memcpy(newHighBuffer, _stdDevHighBuffer, _stdDevBufferCapacity * sizeof(float));
      }

      delete[] _stdDevLowBuffer;
      delete[] _stdDevHighBuffer;
      _stdDevLowBuffer = newLowBuffer;
      _stdDevHighBuffer = newHighBuffer;
      _stdDevBufferCapacity = count;

      return true;
   }

   void _recomputeStdDevBand()
   {
      _refreshStorage();

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

      // Resume the sliding-window scan from the last already-finalized index instead of
      // rescanning from 0 - see the matching comment in _recomputeMovingAverage(). Sum
      // accumulators are double precision - see the comment on _stdDevSum's declaration.
      size_t lo = _stdDevLo;
      size_t hi = _stdDevHi;
      double sum = _stdDevSum;
      double sumSquares = _stdDevSumSquares;
      size_t finiteCount = _stdDevFiniteCount;
      size_t readyCount = _stdDevReadyCount;

      for (size_t i = readyCount; i < _count; i++)
      {
         while (lo < _count && (_x[i] - _x[lo]) > halfWindow)
         {
            if (isfinite(_y[lo]))
            {
               sum -= _y[lo];
               sumSquares -= static_cast<double>(_y[lo]) * _y[lo];
               finiteCount--;
            }
            lo++;
         }

         while (hi < _count && (_x[hi] - _x[i]) <= halfWindow)
         {
            if (isfinite(_y[hi]))
            {
               sum += _y[hi];
               sumSquares += static_cast<double>(_y[hi]) * _y[hi];
               finiteCount++;
            }
            hi++;
         }

         if (!finalized && (!isfinite(_maxSampleX) || ((_x[i] + halfWindow) > _maxSampleX)))
         {
            break;
         }

         if (finiteCount > 0)
         {
            const double mean = sum / static_cast<double>(finiteCount);
            const double meanOfSquares = sumSquares / static_cast<double>(finiteCount);
            const double variance = meanOfSquares - (mean * mean);
            const double stdDev = sqrt((variance > 0.0) ? variance : 0.0);
            _stdDevLowBuffer[i] = static_cast<float>(mean - stdDev);
            _stdDevHighBuffer[i] = static_cast<float>(mean + stdDev);
         }
         else
         {
            _stdDevLowBuffer[i] = NAN;
            _stdDevHighBuffer[i] = NAN;
         }

         readyCount = i + 1;

         // Persist the window state as of this now-finalized index so the next call can
         // resume from here rather than rescanning.
         _stdDevLo = lo;
         _stdDevHi = hi;
         _stdDevSum = sum;
         _stdDevSumSquares = sumSquares;
         _stdDevFiniteCount = finiteCount;
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
   float movingSampleSize = 0.0f;

   bool finalized = false;

   explicit ScatterPlotSeriesBase(size_t capacity = 10)
      : _capacity(capacity)
   {
      if (capacity > 0)
      {
         _x = new (std::nothrow) float[capacity];
         _y = new (std::nothrow) float[capacity];

         if (_x == nullptr || _y == nullptr)
         {
            Util::reset(0.0f, "OOM allocating points in ScatterPlotSeries");
         }
      }
   }

   ScatterPlotSeriesBase(const ScatterPlotSeriesBase&) = delete;
   ScatterPlotSeriesBase& operator=(const ScatterPlotSeriesBase&) = delete;

   ~ScatterPlotSeriesBase() override
   {
      delete[] _x;
      delete[] _y;
      delete[] _movingAverageBuffer;
      delete[] _stdDevLowBuffer;
      delete[] _stdDevHighBuffer;
   }

   void clear() override
   {
      _clearDerivedState();

      _count = 0;
      _movingAverageReadyCount = 0;
      _movingAverageDirty = true;
      _movingAverageLo = 0;
      _movingAverageHi = 0;
      _movingAverageSum = 0.0f;
      _movingAverageFiniteCount = 0;
      _stdDevReadyCount = 0;
      _stdDevDirty = true;
      _stdDevLo = 0;
      _stdDevHi = 0;
      _stdDevSum = 0.0;
      _stdDevSumSquares = 0.0;
      _stdDevFiniteCount = 0;
      finalized = false;
      _nextX = 0.0f;
      _xMin = NAN;
      _xMax = NAN;
      _yMin = NAN;
      _yMax = NAN;
      _maxSampleX = NAN;
      _sumY = 0.0f;
      _sumSquaresY = 0.0f;
      _finiteYCount = 0;
      _statsDirty = false;
      _rangeDirty = false;
   }

   float getX(size_t index)
   {
      _refreshStorage();
      return (index < _count) ? _x[index] : 0.0f;
   }

   float getY(size_t index)
   {
      _refreshStorage();
      return (index < _count) ? _y[index] : 0.0f;
   }

   size_t getCount()
   {
      _refreshStorage();
      return _count;
   }

   void getRawRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax) override
   {
      _refreshStorage();

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

   size_t getMovingAverageReadyCount()
   {
      _recomputeMovingAverage();
      return _movingAverageReadyCount;
   }

   float getMovingAverageY(size_t index)
   {
      _recomputeMovingAverage();
      return (index < _movingAverageReadyCount) ? _movingAverageBuffer[index] : NAN;
   }

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

   void movingAverageRange(float* outMin, float* outMax) override
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

   void stdDevRange(float* outMean, float* outStdDev)
   {
      _refreshStorage();

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

   size_t getStdDevReadyCount()
   {
      _recomputeStdDevBand();
      return _stdDevReadyCount;
   }

   float getStdDevLowY(size_t index)
   {
      _recomputeStdDevBand();
      return (index < _stdDevReadyCount) ? _stdDevLowBuffer[index] : NAN;
   }

   float getStdDevHighY(size_t index)
   {
      _recomputeStdDevBand();
      return (index < _stdDevReadyCount) ? _stdDevHighBuffer[index] : NAN;
   }

   void stdDevBandRange(float* outMin, float* outMax) override
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

   void prepareForRender() override
   {
      _recomputeMovingAverage();
      _recomputeStdDevBand();
   }

   size_t pointCount() const override { return _count; }
   const float* xValues() const override { return _x; }
   const float* yValues() const override { return _y; }

   const float* movingAverageValues() const override
   {
      return (_movingAverageReadyCount > 0) ? _movingAverageBuffer : nullptr;
   }

   const float* stdDevLowValues() const override
   {
      return (_stdDevReadyCount > 0) ? _stdDevLowBuffer : nullptr;
   }

   const float* stdDevHighValues() const override
   {
      return (_stdDevReadyCount > 0) ? _stdDevHighBuffer : nullptr;
   }

   bool getFixedXRange(float* outMin, float* outMax) const override
   {
      if (!_hasFixedXRange)
      {
         return false;
      }

      *outMin = _fixedXMin;
      *outMax = _fixedXMax;
      return true;
   }

   void setFixedXRange(float xMin, float xMax)
   {
      _hasFixedXRange = (xMax > xMin);
      _fixedXMin = xMin;
      _fixedXMax = xMax;
   }

   float getLatestMovingAverage() override
   {
      _recomputeMovingAverage();
      return (_movingAverageReadyCount > 0) ? _movingAverageBuffer[_movingAverageReadyCount - 1] : NAN;
   }

   float getLatestStdDev() override
   {
      float mean;
      float stdDev;
      stdDevRange(&mean, &stdDev);
      return stdDev;
   }
};
