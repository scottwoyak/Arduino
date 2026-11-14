//
// AC and 5V DC voltage prototype display.
//
// Continuously samples a ZMPT101B AC mains voltage sensor over 3 complete 60 Hz cycles
// (50 ms) to compute DC-removed true RMS line voltage, ADC AC signal RMS, and DC bias,
// alongside a 5V DC supply rail measurement, displaying all readings on the TFT screen
// and logging a periodic summary table over Serial.
//
// Feather ESP32-S3 TFT Power & Breadboard Wiring (USB-Powered, 9V AC Wall-Wart Input):
// - Where 5V DC output exists on the Feather:
//     * The pin labeled "USB" (top 12-pin header edge, 3rd pin from USB-C: BAT, EN, USB)
//       is directly connected to USB VBUS and outputs ~5.0V DC whenever USB is plugged in.
// - ZMPT101B AC Voltage Sensor (fed by 9V AC supply, wired directly to A0 at 3.3V):
//     * ZMPT101B AC screw terminals -> 9V AC wall-wart (either polarity; may share in
//       parallel with LM2596HV AC input since ZMPT101B is galvanically isolated)
//     * ZMPT101B VCC -> Feather 3V (3.3V DC)
//     * ZMPT101B GND -> Feather GND
//     * ZMPT101B OUT -> Feather A0 directly (no divider needed for 9V AC input; optional
//       100k-1M pull-down from A0 to GND for unplug detection)
//     * Turn the ZMPT101B trimpot to ~0.100V RMS at "AC Sig" (riding on ~1.65V at "AC Bias")
//       until "AC Line" matches a multimeter reading of your 120V wall outlet.
// - 5V DC Rail Monitor (on A1 via 2:1 resistor divider):
//     * Feather "USB" pin (or pre-adjusted LM2596HV OUT+ <= 6.5V, with OUT- tied to GND;
//       never tie external 5V directly to the "USB" pin while USB is plugged in)
//       -> 10k ohm resistor -> Feather A1
//     * Feather A1 -> 10k ohm resistor -> Feather GND
//

#include <Arduino.h>
#include <array>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 TFT)."
#include "WrongBoard.h"
#endif

#include "DisplayValue.h"
#include "RollingAverage.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "Table.h"
#include "Timer.h"

// ----------- Pins
constexpr uint8_t AC_VOLTAGE_PIN = A0;
constexpr uint8_t DC_5V_PIN = A1;

// ----------- Sampling & Calibration
constexpr uint32_t AC_SAMPLE_SPAN_MICROS = 50000;  // 3 complete cycles at 60 Hz
constexpr uint8_t DC_SAMPLES_PER_SLICE = 16;
constexpr size_t ROLLING_WINDOW_SLICES = 10;       // 0.5 s rolling smoothing window

// Default scale assumes ZMPT101B (fed from the 9V AC wall-wart) has its trimpot set to
// 0.100V RMS at A0 when the wall outlet is at 120V AC (leaving headroom up to ~0.115V max).
constexpr float AC_VOLTS_PER_ADC_VOLT = 120.0f / 0.100f;
constexpr float DC_DIVIDER_RATIO = 2.0f;           // 10k / 10k divider on A1

// ----------- Signal Health Thresholds
constexpr float MIN_VALID_AC_BIAS_V = 0.20f;
constexpr float MAX_VALID_AC_BIAS_V = 3.10f;
constexpr float AC_SIGNAL_NOISE_FLOOR_V = 0.005f;
constexpr float ADC_CLIP_LOW_V = 0.08f;
constexpr float ADC_CLIP_HIGH_V = 3.10f;
constexpr float MIN_CONNECTED_DC_PIN_V = 0.15f;
constexpr float MIN_NOMINAL_5V_V = 4.75f;
constexpr float MAX_NOMINAL_5V_V = 5.25f;

// ----------- Update Rates
constexpr float DISPLAY_UPDATE_RATE = 5.0f;
constexpr float SERIAL_LOG_RATE = 1.0f;

// ----------- Table Row Indices
constexpr size_t ROW_AC_LINE = 0;
constexpr size_t ROW_AC_SIGNAL = 1;
constexpr size_t ROW_AC_BIAS = 2;
constexpr size_t ROW_DC_5V = 3;

// ----------- Serial Logging Columns
constexpr size_t NUM_SERIAL_COLUMNS = 6;
constexpr std::array<SerialTable::Column, NUM_SERIAL_COLUMNS> SERIAL_COLUMNS = {{
   { "AC Line (V)", 13 },
   { "AC Sig (V)",  12 },
   { "AC Bias (V)", 13 },
   { "5V Rail (V)", 13 },
   { "Status",      11 },
   { "Rate (/s)",   10 },
}};

// ----------- Global State
Arduino arduino;
Table table(&arduino, 0, 0);
Format rateFormat("#####/s", Format::Alignment::RIGHT);
DisplayValue rateField(
   &arduino,
   rateFormat,
   DEFAULT_FOOTER_SIZE,
   DisplayValue::Alignment::RIGHT);
SerialTable serialTable("AC Voltage Monitor", SERIAL_COLUMNS);
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
RateTimer serialLogTimer(SERIAL_LOG_RATE);
RollingAverage acSignalAvg(ROLLING_WINDOW_SLICES);
RollingAverage acBiasAvg(ROLLING_WINDOW_SLICES);
RollingAverage dcPinAvg(ROLLING_WINDOW_SLICES);

///
/// <summary>
/// Summary measurements captured across one multi-cycle AC sampling slice.
/// </summary>
///
struct AcSliceSample
{
   float dcBiasVolts = 0.0f;
   float acSignalRmsVolts = 0.0f;
   float minVolts = 0.0f;
   float maxVolts = 0.0f;
   uint32_t samplesPerSec = 0;
};

///
/// <summary>
/// Reads calibrated voltage in volts from an analog input pin.
/// </summary>
/// <param name="pin">Analog pin to sample</param>
/// <returns>Measured pin voltage in volts (0.0V - 3.3V)</returns>
///
float readPinVolts(uint8_t pin)
{
#ifdef ARDUINO_ARCH_ESP32
   return analogReadMilliVolts(pin) * 0.001f;
#else
   constexpr float ADC_REF_VOLTS = 3.3f;
   constexpr float ADC_MAX_COUNTS = 4095.0f;
   return analogRead(pin) * (ADC_REF_VOLTS / ADC_MAX_COUNTS);
#endif
}

///
/// <summary>
/// Samples the AC voltage input continuously across 3 complete 60 Hz cycles (50 ms) and
/// computes DC bias, DC-removed AC RMS signal voltage, peak bounds, and sample rate.
/// </summary>
/// <returns>Captured AC slice metrics</returns>
///
AcSliceSample sampleAcSlice()
{
   double sumVolts = 0.0;
   double sumSquaredVolts = 0.0;
   float minVolts = 3.3f;
   float maxVolts = 0.0f;
   uint32_t count = 0;

   TimerMicros spanTimer(AC_SAMPLE_SPAN_MICROS);
   while (!spanTimer.ready())
   {
      float volts = readPinVolts(AC_VOLTAGE_PIN);
      sumVolts += volts;
      sumSquaredVolts += volts * volts;
      if (volts < minVolts)
      {
         minVolts = volts;
      }
      if (volts > maxVolts)
      {
         maxVolts = volts;
      }
      count++;
   }

   AcSliceSample result;
   if (count == 0)
   {
      return result;
   }

   double meanVolts = sumVolts / count;
   double meanSquaredVolts = sumSquaredVolts / count;
   float variance = meanSquaredVolts - (meanVolts * meanVolts);

   result.dcBiasVolts = meanVolts;
   result.acSignalRmsVolts = sqrtf(fmaxf(0.0f, variance));
   result.minVolts = minVolts;
   result.maxVolts = maxVolts;
   result.samplesPerSec = count * (1000000UL / AC_SAMPLE_SPAN_MICROS);
   return result;
}

///
/// <summary>
/// Reads and averages the 5V DC rail divider input pin across a short burst of samples.
/// </summary>
/// <returns>Averaged pin voltage at DC_5V_PIN before divider scaling</returns>
///
float readDcRailPinVolts()
{
   float sumVolts = 0.0f;
   for (uint8_t i = 0; i < DC_SAMPLES_PER_SLICE; i++)
   {
      sumVolts += readPinVolts(DC_5V_PIN);
   }
   return sumVolts / DC_SAMPLES_PER_SLICE;
}

void setup()
{
   SerialX::begin();
   arduino.begin();

#ifndef ARDUINO_ARCH_ESP32
   analogReadResolution(12);
#endif

   pinMode(AC_VOLTAGE_PIN, INPUT);
   pinMode(DC_5V_PIN, INPUT);

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("AC Voltage", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 4);

   table.setPosition(0, arduino.getCursorY());
   table.addRow("AC Line:", "#####.# V");
   table.addRow("AC Sig:", "#.### V");
   table.addRow("AC Bias:", "#.## V");
   table.addRow("5V Rail:", "#.## V");

   arduino.setTextSize(DEFAULT_FOOTER_SIZE);
   int16_t footerY = arduino.height() - arduino.charH();
   arduino.setCursor(0, footerY);
   arduino.print("A0:AC  A1:5V", Color::LIGHTGRAY);
   rateField.setPosition(arduino.width(), footerY);

   serialTable.printHeader(false);
}

void loop()
{
   AcSliceSample slice = sampleAcSlice();
   acSignalAvg.set(slice.acSignalRmsVolts);
   acBiasAvg.set(slice.dcBiasVolts);
   dcPinAvg.set(readDcRailPinVolts());

   float avgBiasVolts = acBiasAvg.get();
   float avgSignalVolts = acSignalAvg.get();
   float avgDcPinVolts = dcPinAvg.get();

   bool acConnected = (avgBiasVolts >= MIN_VALID_AC_BIAS_V) && (avgBiasVolts <= MAX_VALID_AC_BIAS_V);
   bool acClipping = acConnected &&
      (avgSignalVolts > AC_SIGNAL_NOISE_FLOOR_V) &&
      ((slice.minVolts <= ADC_CLIP_LOW_V) || (slice.maxVolts >= ADC_CLIP_HIGH_V));

   float cleanSignalVolts = (avgSignalVolts > AC_SIGNAL_NOISE_FLOOR_V) ? avgSignalVolts : 0.0f;
   float acLineVolts = cleanSignalVolts * AC_VOLTS_PER_ADC_VOLT;

   bool dcConnected = avgDcPinVolts >= MIN_CONNECTED_DC_PIN_V;
   float dcRailVolts = avgDcPinVolts * DC_DIVIDER_RATIO;

   const char* statusText = "OK";
   if (!acConnected)
   {
      statusText = "Unplugged";
   }
   else if (acClipping)
   {
      statusText = "Clipping!";
   }

   if (serialLogTimer.ready())
   {
      serialTable.printRow(
         false,
         SerialTable::fixed(acConnected ? acLineVolts : 0.0f, 1),
         SerialTable::fixed(acConnected ? avgSignalVolts : 0.0f, 3),
         SerialTable::fixed(avgBiasVolts, 2),
         SerialTable::fixed(dcConnected ? dcRailVolts : 0.0f, 2),
         statusText,
         slice.samplesPerSec);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   if (!acConnected)
   {
      table.setValue(ROW_AC_LINE, "Unplugged", Color::GRAY);
      table.setValueNone(ROW_AC_SIGNAL, Color::GRAY);
      table.setValue(ROW_AC_BIAS, avgBiasVolts, Color::GRAY);
   }
   else if (acClipping)
   {
      table.setValue(ROW_AC_LINE, "Clipping!", Color::RED);
      table.setValue(ROW_AC_SIGNAL, avgSignalVolts, Color::RED);
      table.setValue(ROW_AC_BIAS, avgBiasVolts, Color::VALUE);
   }
   else
   {
      table.setValue(ROW_AC_LINE, acLineVolts, Color::VALUE);
      table.setValue(ROW_AC_SIGNAL, avgSignalVolts, Color::VALUE);
      table.setValue(ROW_AC_BIAS, avgBiasVolts, Color::VALUE);
   }

   if (!dcConnected)
   {
      table.setValueNone(ROW_DC_5V, Color::GRAY);
   }
   else
   {
      bool nominal5v = (dcRailVolts >= MIN_NOMINAL_5V_V) && (dcRailVolts <= MAX_NOMINAL_5V_V);
      table.setValue(ROW_DC_5V, dcRailVolts, nominal5v ? Color::VALUE : Color::YELLOW);
   }

   table.draw();
   rateField.draw(slice.samplesPerSec, Color::LIGHTGRAY);
}
