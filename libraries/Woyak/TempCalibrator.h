#pragma once

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <stdio.h>

#include "SerialTable.h"

///
/// <summary>
/// Manages sensor calibration by collecting measurements and computing correction factors.
/// Assumes all sensors should read equal values and computes per-sensor offsets to align readings.
/// </summary>
///
class TempCalibrator
{
public:
   ///
   /// <summary>
   /// Baseline computation mode.
   /// </summary>
   ///
   enum class BaselineMode
   {
      AVERAGE,
      FIRST_SENSOR
   };

   ///
   /// <summary>
   /// A single recorded calibration point: the whole-number baseline temperature
   /// and the correction factor captured for each sensor at that baseline.
   /// </summary>
   ///
   struct CalibrationPoint
   {
      int16_t baseline;
      float corrections[8];
   };

   ///
   /// <summary>
   /// Result of fitting a curve (correction vs. baseline) for one sensor.
   /// </summary>
   ///
   struct RegressionFit
   {
      double a = 0.0;         ///< Constant term
      double b = 0.0;         ///< Linear term coefficient
      double c = 0.0;         ///< Quadratic term coefficient (0 for a linear fit)
      double rSquared = 0.0;  ///< Coefficient of determination (goodness of fit)
      bool isQuadratic = false;  ///< True if this is a quadratic fit, false if linear
      bool valid = false;     ///< True if enough data existed to compute this fit
   };

private:
   static constexpr uint8_t MAX_SENSORS = 8;
   static constexpr uint8_t MAX_CALIBRATION_POINTS = 128;

   // A quadratic fit is only recommended over a linear fit if it improves R-squared
   // by at least this much; otherwise the simpler linear fit is preferred.
   static constexpr double QUADRATIC_R_SQUARED_IMPROVEMENT_THRESHOLD = 0.01;

   uint8_t _numSensors = 0;
   float _values[MAX_SENSORS];
   BaselineMode _baselineMode = BaselineMode::AVERAGE;

   CalibrationPoint _calibrationPoints[MAX_CALIBRATION_POINTS];
   uint8_t _calibrationPointCount = 0;
   float _nextThreshold = NAN;

   ///
   /// <summary>
   /// Computes the baseline according to the configured mode.
   /// </summary>
   /// <returns>Baseline value, or NaN if no measurements exist.</returns>
   ///
   float _getBaseline() const
   {
      if (_baselineMode == BaselineMode::FIRST_SENSOR)
      {
         return _values[0];
      }

      float sum = 0.0f;
      int count = 0;

      for (uint8_t i = 0; i < _numSensors; i++)
      {
         float val = _values[i];
         if (!std::isnan(val))
         {
            sum += val;
            count++;
         }
      }

      return (count > 0) ? (sum / count) : NAN;
   }

   ///
   /// <summary>
   /// Computes and returns the correction factor for a sensor.
   /// Correction = (baseline - sensorValue), so applying this offset makes the sensor read baseline.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Correction factor, or NaN if measurement unavailable.</returns>
   ///
   float _computeCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return NAN;

      float baseline = _getBaseline();
      float measurement = _values[sensorIndex];

      if (std::isnan(baseline) || std::isnan(measurement))
         return NAN;

      return baseline - measurement;
   }

   ///
   /// <summary>
   /// Checks whether the (monotonically increasing) baseline has reached the next
   /// whole-number threshold and, if so, records the correction factor for every
   /// sensor at that threshold. Handles the case where the baseline jumps past
   /// more than one whole-number value between calls.
   /// </summary>
   ///
   void _checkCalibrationThreshold()
   {
      float baseline = _getBaseline();
      if (std::isnan(baseline))
      {
         return;
      }

      if (std::isnan(_nextThreshold))
      {
         _nextThreshold = std::ceil(baseline);
      }

      while (baseline >= _nextThreshold && _calibrationPointCount < MAX_CALIBRATION_POINTS)
      {
         CalibrationPoint& point = _calibrationPoints[_calibrationPointCount];
         point.baseline = static_cast<int16_t>(_nextThreshold);

         for (uint8_t i = 0; i < MAX_SENSORS; i++)
         {
            point.corrections[i] = (i < _numSensors) ? getCorrection(i) : 0.0f;
         }

         _calibrationPointCount++;
         _nextThreshold += 1.0f;
      }
   }

   ///
   /// <summary>
   /// Solves a 3x3 linear system m * solution = rhs using Cramer's rule.
   /// </summary>
   /// <param name="m">3x3 coefficient matrix.</param>
   /// <param name="rhs">Right-hand-side vector.</param>
   /// <param name="solution">Receives the solved values.</param>
   /// <returns>True if the system was solvable (non-zero determinant).</returns>
   ///
   static bool _solve3x3(const double m[3][3], const double rhs[3], double solution[3])
   {
      double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                 - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

      if (std::fabs(det) < 1e-9)
      {
         return false;
      }

      for (uint8_t col = 0; col < 3; col++)
      {
         double a[3][3];
         for (uint8_t r = 0; r < 3; r++)
         {
            for (uint8_t c = 0; c < 3; c++)
            {
               a[r][c] = (c == col) ? rhs[r] : m[r][c];
            }
         }

         double colDet = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
                       - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                       + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);

         solution[col] = colDet / det;
      }

      return true;
   }

   ///
   /// <summary>
   /// Computes least-squares linear (y = a + b*x) and quadratic (y = a + b*x + c*x^2)
   /// fits of a sensor's correction factor as a function of the baseline temperature,
   /// along with each fit's R-squared (coefficient of determination).
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <param name="linearFit">Receives the linear fit result.</param>
   /// <param name="quadraticFit">Receives the quadratic fit result.</param>
   ///
   void _computeFits(uint8_t sensorIndex, RegressionFit& linearFit, RegressionFit& quadraticFit) const
   {
      linearFit = RegressionFit();
      quadraticFit = RegressionFit();

      uint8_t n = _calibrationPointCount;
      if (n < 2)
      {
         return;
      }

      double sumX = 0.0, sumX2 = 0.0, sumX3 = 0.0, sumX4 = 0.0;
      double sumY = 0.0, sumXY = 0.0, sumX2Y = 0.0;

      for (uint8_t i = 0; i < n; i++)
      {
         double x = _calibrationPoints[i].baseline;
         double y = _calibrationPoints[i].corrections[sensorIndex];
         double x2 = x * x;

         sumX += x;
         sumX2 += x2;
         sumX3 += x2 * x;
         sumX4 += x2 * x2;
         sumY += y;
         sumXY += x * y;
         sumX2Y += x2 * y;
      }

      double meanY = sumY / n;
      double totalSS = 0.0;
      for (uint8_t i = 0; i < n; i++)
      {
         double diff = _calibrationPoints[i].corrections[sensorIndex] - meanY;
         totalSS += diff * diff;
      }

      // Linear fit: y = a + b*x
      double linearDenom = (n * sumX2 - sumX * sumX);
      if (linearDenom != 0.0)
      {
         double b = (n * sumXY - sumX * sumY) / linearDenom;
         double a = (sumY - b * sumX) / n;

         double residualSS = 0.0;
         for (uint8_t i = 0; i < n; i++)
         {
            double x = _calibrationPoints[i].baseline;
            double residual = _calibrationPoints[i].corrections[sensorIndex] - (a + b * x);
            residualSS += residual * residual;
         }

         linearFit.a = a;
         linearFit.b = b;
         linearFit.c = 0.0;
         linearFit.rSquared = (totalSS > 0.0) ? (1.0 - residualSS / totalSS) : 1.0;
         linearFit.isQuadratic = false;
         linearFit.valid = true;
      }

      // Quadratic fit: y = a + b*x + c*x^2, via the normal equations solved as a 3x3 system
      if (n >= 3)
      {
         double m[3][3] =
         {
            { static_cast<double>(n), sumX,  sumX2 },
            { sumX,                   sumX2, sumX3 },
            { sumX2,                  sumX3, sumX4 }
         };
         double rhs[3] = { sumY, sumXY, sumX2Y };
         double solution[3];

         if (_solve3x3(m, rhs, solution))
         {
            double a = solution[0];
            double b = solution[1];
            double c = solution[2];

            double residualSS = 0.0;
            for (uint8_t i = 0; i < n; i++)
            {
               double x = _calibrationPoints[i].baseline;
               double residual = _calibrationPoints[i].corrections[sensorIndex] - (a + b * x + c * x * x);
               residualSS += residual * residual;
            }

            quadraticFit.a = a;
            quadraticFit.b = b;
            quadraticFit.c = c;
            quadraticFit.rSquared = (totalSS > 0.0) ? (1.0 - residualSS / totalSS) : 1.0;
            quadraticFit.isQuadratic = true;
            quadraticFit.valid = true;
         }
      }
   }

   ///
   /// <summary>
   /// Picks the recommended fit for a sensor: quadratic if it improves R-squared over
   /// linear by more than QUADRATIC_R_SQUARED_IMPROVEMENT_THRESHOLD, linear otherwise.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>The recommended fit, or an invalid fit if there wasn't enough data.</returns>
   ///
   RegressionFit _getBestFit(uint8_t sensorIndex) const
   {
      RegressionFit linearFit;
      RegressionFit quadraticFit;
      _computeFits(sensorIndex, linearFit, quadraticFit);

      if (quadraticFit.valid && (!linearFit.valid || quadraticFit.rSquared > linearFit.rSquared + QUADRATIC_R_SQUARED_IMPROVEMENT_THRESHOLD))
      {
         return quadraticFit;
      }

      return linearFit;
   }

public:
   ///
   /// <summary>
   /// Constructs a Calibrator for the specified number of sensors. Expects already-smoothed
   /// values to be provided via set() (e.g. from an upstream rolling/timed average); this
   /// class does not perform any averaging of its own.
   /// </summary>
   /// <param name="numSensors">Number of sensors (1-8).</param>
   /// <param name="mode">Baseline computation mode (default: AVERAGE).</param>
   ///
   TempCalibrator(uint8_t numSensors, BaselineMode mode = BaselineMode::AVERAGE)
      : _numSensors(std::min(numSensors, MAX_SENSORS)), _baselineMode(mode)
   {
      for (uint8_t i = 0; i < MAX_SENSORS; i++)
      {
         _values[i] = NAN;
      }
   }

   ///
   /// <summary>
   /// Adds a measurement for the specified sensor. The value is expected to already be
   /// smoothed (e.g. a rolling or timed average) rather than a raw instantaneous reading.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <param name="value">Measurement value to record.</param>
   ///
   void set(uint8_t sensorIndex, float value)
   {
      if (sensorIndex < _numSensors && !std::isnan(value))
      {
         _values[sensorIndex] = value;
         _checkCalibrationThreshold();
      }
   }

   ///
   /// <summary>
   /// Gets the number of sensors managed by this Calibrator.
   /// </summary>
   /// <returns>Number of sensors.</returns>
   ///
   uint8_t getNumSensors() const
   {
      return _numSensors;
   }

   ///
   /// <summary>
   /// Gets the current averaged measurement for a sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Averaged measurement, or NaN if unavailable.</returns>
   ///
   float getAverage(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return NAN;

      return _values[sensorIndex];
   }

   ///
   /// <summary>
   /// Gets the current baseline value according to the configured mode.
   /// </summary>
   /// <returns>Baseline value, or NaN if no measurements exist.</returns>
   ///
   float getBaseline() const
   {
      return _getBaseline();
   }

   ///
   /// <summary>
   /// Gets the current correction factor for a sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Correction factor, or 0 if unavailable.</returns>
   ///
   float getCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return 0.0f;

      float correction = _computeCorrection(sensorIndex);
      return std::isnan(correction) ? 0.0f : correction;
   }

   ///
   /// <summary>
   /// Gets the number of whole-number baseline points recorded so far.
   /// </summary>
   /// <returns>Number of recorded calibration points.</returns>
   ///
   uint8_t getCalibrationPointCount() const
   {
      return _calibrationPointCount;
   }

   ///
   /// <summary>
   /// Gets a recorded calibration point. Points are recorded in ascending baseline
   /// order since the baseline is assumed to increase monotonically during calibration.
   /// </summary>
   /// <param name="index">Zero-based index of the recorded point.</param>
   /// <returns>The calibration point, or a point with baseline 0 and all-zero corrections if the index is invalid.</returns>
   ///
   CalibrationPoint getCalibrationPoint(uint8_t index) const
   {
      if (index >= _calibrationPointCount)
      {
         return CalibrationPoint{ 0, { 0.0f } };
      }

      return _calibrationPoints[index];
   }

   ///
   /// <summary>
   /// Resets all recorded calibration points and the next threshold to record at
   /// (does not affect current measurements).
   /// </summary>
   ///
   void resetCalibrationPoints()
   {
      _calibrationPointCount = 0;
      _nextThreshold = NAN;
   }

   ///
   /// <summary>
   /// Builds the list of included sensors and matching table columns for calibration
   /// table printing, shared by printCalibrationHeader(), printCalibrationRow(), and
   /// printCalibrationTable().
   /// </summary>
   /// <param name="sensorExists">Optional array (indexed by sensor) indicating which sensors are
   /// present; only those sensors get a column. Pass nullptr to include every managed sensor.</param>
   /// <param name="sensorLabels">Storage (size MAX_SENSORS) for the generated column label strings.</param>
   /// <param name="columns">Storage (size MAX_SENSORS + 1) for the generated column metadata.</param>
   /// <param name="includedSensors">Storage (size MAX_SENSORS) receiving the sensor index for each included column.</param>
   /// <param name="includedCount">Receives the number of sensors included as columns.</param>
   ///
   void _buildCalibrationColumns(const bool* sensorExists, String* sensorLabels, SerialTable::Column* columns,
      uint8_t* includedSensors, uint8_t& includedCount) const
   {
      includedCount = 0;
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         if (sensorExists == nullptr || sensorExists[i])
         {
            includedSensors[includedCount++] = i;
         }
      }

      columns[0] = { "Temp", 8 };
      for (uint8_t c = 0; c < includedCount; c++)
      {
         sensorLabels[c] = "S" + String(includedSensors[c]);
         columns[c + 1] = { sensorLabels[c].c_str(), 9 };
      }
   }

public:
   ///
   /// <summary>
   /// Prints just the "Correction Values Table" title and column header row, without any
   /// data rows. Pair with printCalibrationRow() to stream rows as they're recorded rather
   /// than reprinting the whole table each time.
   /// </summary>
   /// <param name="sensorExists">Optional array (indexed by sensor) indicating which sensors are
   /// present; only those sensors get a column. Pass nullptr to include every managed sensor.</param>
   ///
   void printCalibrationHeader(const bool* sensorExists = nullptr) const
   {
      String sensorLabels[MAX_SENSORS];
      SerialTable::Column columns[MAX_SENSORS + 1];
      uint8_t includedSensors[MAX_SENSORS];
      uint8_t includedCount;
      _buildCalibrationColumns(sensorExists, sensorLabels, columns, includedSensors, includedCount);

      if (includedCount == 0)
      {
         return;
      }

      SerialTable table("Correction Values Table", columns, includedCount + 1);
      table.printHeader();
   }

   ///
   /// <summary>
   /// Prints a single recorded calibration point as one table row, matching the column
   /// layout produced by printCalibrationHeader(). Intended to be called once per new row
   /// as it's recorded, immediately after printCalibrationHeader() has been called once.
   /// </summary>
   /// <param name="index">Zero-based index of the recorded point to print.</param>
   /// <param name="sensorExists">Optional array (indexed by sensor) indicating which sensors are
   /// present; only those sensors get a column. Pass nullptr to include every managed sensor.</param>
   ///
   void printCalibrationRow(uint8_t index, const bool* sensorExists = nullptr) const
   {
      if (index >= _calibrationPointCount)
      {
         return;
      }

      String sensorLabels[MAX_SENSORS];
      SerialTable::Column columns[MAX_SENSORS + 1];
      uint8_t includedSensors[MAX_SENSORS];
      uint8_t includedCount;
      _buildCalibrationColumns(sensorExists, sensorLabels, columns, includedSensors, includedCount);

      if (includedCount == 0)
      {
         return;
      }

      const CalibrationPoint& point = _calibrationPoints[index];

      SerialX::print(String(point.baseline) + "F", columns[0].width);
      for (uint8_t c = 0; c < includedCount; c++)
      {
         float correction = point.corrections[includedSensors[c]];
         if (c + 1 == includedCount)
         {
            SerialX::println(correction, 3, columns[c + 1].width);
         }
         else
         {
            SerialX::print(correction, 3, columns[c + 1].width);
         }
      }
   }

   ///
   /// <summary>
   /// Prints all recorded calibration points, in ascending baseline order, to Serial
   /// as a table. One column is printed per sensor, containing that sensor's
   /// correction factor at each recorded baseline temperature.
   /// </summary>
   /// <param name="sensorExists">Optional array (indexed by sensor) indicating which sensors are
   /// present; only those sensors get a column. Pass nullptr to include every managed sensor.</param>
   ///
   void printCalibrationTable(const bool* sensorExists = nullptr) const
   {
      if (_calibrationPointCount == 0)
      {
         return;
      }

      printCalibrationHeader(sensorExists);
      for (uint8_t i = 0; i < _calibrationPointCount; i++)
      {
         printCalibrationRow(i, sensorExists);
      }

      //printFitAnalysis();
   }

   ///
   /// <summary>
   /// Fits each sensor's correction factor as a function of baseline temperature with both
   /// a linear and a quadratic curve, and prints a table recommending the better fit (based
   /// on R-squared) along with its coefficients for use by an interpolator.
   /// </summary>
   ///
   void printFitAnalysis() const
   {
      if (_calibrationPointCount == 0)
      {
         return;
      }

      static const SerialTable::Column columns[] =
      {
         { "S#",   4 },
         { "Fit",  10 },
         { "a",    10 },
         { "b",    10 },
         { "c",    10 },
         { "R^2",  8 }
      };

      SerialTable table("Recommended Fit (correction = a + b*baseline + c*baseline^2)", columns, 6);
      table.printHeader();

      for (uint8_t i = 0; i < _numSensors; i++)
      {
         RegressionFit fit = _getBestFit(i);

         if (!fit.valid)
         {
            table.printRow(i, "N/A", "---", "---", "---", "---");
            continue;
         }

         table.printRow(i,
            fit.isQuadratic ? "Quadratic" : "Linear",
            SerialTable::fixed(static_cast<float>(fit.a), 4),
            SerialTable::fixed(static_cast<float>(fit.b), 4),
            SerialTable::fixed(static_cast<float>(fit.c), 4),
            SerialTable::fixed(static_cast<float>(fit.rSquared), 4));
      }
   }
};
