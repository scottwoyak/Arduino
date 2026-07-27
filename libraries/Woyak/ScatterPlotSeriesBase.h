#pragma once

#include "Color.h"
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

   float* _stdDevLowBuffer = nullptr;
   float* _stdDevHighBuffer = nullptr;
   size_t _stdDevBufferCapacity = 0;
   size_t _stdDevReadyCount = 0;
   bool _stdDevDirty = true;

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

      delete[] _movingAverageBuffer;
      _movingAverageBuffer = new (std::nothrow) float[count];
      _movingAverageBufferCapacity = (_movingAverageBuffer != nullptr) ? count : 0;

      return _movingAverageBuffer != nullptr;
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

      size_t lo = 0;
      size_t hi = 0;
      float sum = 0.0f;
      size_t finiteCount = 0;
      size_t readyCount = 0;

      for (size_t i = 0; i < _count; i++)
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
   /// Shifts the cached moving-average and stddev-band buffers left by one slot to mirror
   /// a rolling-count series dropping its oldest raw sample: since those buffers are
   /// indexed by fixed slot position and the underlying window shape is invariant under a
   /// uniform shift, the value previously computed for slot i + 1 is still correct for
   /// slot i, so it is reused instead of being recomputed. Only the newly exposed last
   /// slot may need attention - it stays NaN (matching its prior not-ready state) unless
   /// finalized, in which case a full recompute is requested to fill it in with an
   /// available, boundary-clipped window.
   /// </summary>
   /// <param name="currentCount">Number of valid slots in the rolling buffer (its fixed capacity).</param>
   ///
   void _rollOverlaysLeft(size_t currentCount)
   {
      if (currentCount == 0)
      {
         return;
      }

      if (_movingAverageBuffer != nullptr && (currentCount <= _movingAverageBufferCapacity))
      {
         if (currentCount > 1)
         {
            memmove(_movingAverageBuffer, _movingAverageBuffer + 1, (currentCount - 1) * sizeof(float));
         }
         if (_movingAverageReadyCount > 0)
         {
            _movingAverageReadyCount--;
         }
      }

      if ((_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr) && (currentCount <= _stdDevBufferCapacity))
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
      }

      if (finalized)
      {
         // The last slot is newly exposed and has never been computed with a
         // boundary-clipped window; request a full recompute to fill it in.
         _movingAverageDirty = true;
         _stdDevDirty = true;
      }
      else
      {
         // Matches the not-ready (NaN) state the last slot already had before the
         // shift, since a non-finalized series can't confirm its trailing window yet.
         if (_movingAverageBuffer != nullptr && (currentCount <= _movingAverageBufferCapacity))
         {
            _movingAverageBuffer[currentCount - 1] = NAN;
         }
         if ((_stdDevLowBuffer != nullptr) && (_stdDevHighBuffer != nullptr) && (currentCount <= _stdDevBufferCapacity))
         {
            _stdDevLowBuffer[currentCount - 1] = NAN;
            _stdDevHighBuffer[currentCount - 1] = NAN;
         }
      }
   }

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

      size_t lo = 0;
      size_t hi = 0;
      float sum = 0.0f;
      float sumSquares = 0.0f;
      size_t finiteCount = 0;
      size_t readyCount = 0;

      for (size_t i = 0; i < _count; i++)
      {
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

         if (!finalized && (!isfinite(_maxSampleX) || ((_x[i] + halfWindow) > _maxSampleX)))
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
            Util::setHaltReason("OOM allocating points in ScatterPlotSeries");
            Util::reset();
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
      _stdDevReadyCount = 0;
      _stdDevDirty = true;
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
