#pragma once

// Out-of-line definitions for OTAUpdater methods that touch ArduinoWithDisplay. These are
// split out of OTAUpdater.h to break a circular include: ArduinoWithDisplay.h needs
// OTAUpdater's full class definition (to declare/construct its _ota member), while these
// particular methods need ArduinoWithDisplay's full class definition (to call its display
// methods). See OTAUpdater.h and ArduinoWithDisplay.h for the corresponding include order.

#include "OTAUpdater.h"

inline void OTAUpdater::_reportMissingPartitionAndHalt()
{
   constexpr auto MESSAGE = "OTAUpdater: no OTA download partition found";

   _log(MESSAGE);

#ifdef ARDUINO_DISPLAY_SUPPORTED
   if (_arduino != nullptr)
   {
      _arduino->printInitHeader("OTA Update");
      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->println(MESSAGE, Color::RED);
   }
#endif

   if (_status != nullptr)
   {
      _status->setStatus(Status::FAILED);
   }

   Util::reset(0.0f, MESSAGE);
}

#ifdef ARDUINO_DISPLAY_SUPPORTED
#include "ArduinoWithDisplay.h"
#include "ColorX.h"
#endif

inline void OTAUpdater::_onUpdateProgress(int current, int total)
{
   // Give other tasks (including the idle tasks, whose watchdog is disabled for the
   // duration of the download in _performUpdate() below) a chance to run between chunks.
   yield();

#ifdef ARDUINO_DISPLAY_SUPPORTED
   if (_arduino != nullptr)
   {
      uint8_t percent = (total > 0) ? (current * 100 / total) : 0;

      // Always allow the very first (0%) and last (100%) draws through immediately, so
      // the bar reliably shows its start/end state; throttle everything in between by
      // elapsed time (rather than percent change) so the number of draws stays roughly
      // constant regardless of firmware size/download speed.
      uint32_t nowMs = millis();
      if (percent == _lastDrawnPercent)
      {
         return;
      }
      if (percent != 0 && percent != 100 && (nowMs - _lastDrawTimeMs) < _PROGRESS_REDRAW_INTERVAL_MS)
      {
         return;
      }
      _lastDrawTimeMs = nowMs;
      _lastDrawnPercent = percent;

      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->setCursorY(_downloadRowY);
      _arduino->printR((float)percent, _percentFormat, Color::VALUE);

      int16_t barWidth = _arduino->width() - 2 * _PROGRESS_BAR_MARGIN;
      int16_t fillWidth = barWidth * percent / 100;
      _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, fillWidth, _PROGRESS_BAR_HEIGHT, Color::LIME);

      // Make sure this draw's SPI transaction is fully finished before returning control
      // to _performUpdate(), which then reads/writes the next chunk. _performUpdate() now
      // calls Update.write() (which briefly disables both cores' flash caches) only in
      // between calls to this method, never during one, so this simply guarantees no
      // draw is left half-finished when that happens.
      _arduino->display.waitDisplay();
   }
#endif
}

#ifdef ARDUINO_DISPLAY_SUPPORTED
inline void OTAUpdater::_clearDisplayIfPresent()
{
   if (_arduino != nullptr)
   {
      _arduino->clearDisplay();
   }
}
#endif

inline void OTAUpdater::_performUpdate()
{
#ifdef ARDUINO_DISPLAY_SUPPORTED
   _lastDrawnPercent = -1;
   _lastDrawTimeMs = 0;
#endif

#ifdef ARDUINO_DISPLAY_SUPPORTED
   if (_arduino != nullptr)
   {
      _arduino->printInitHeader("Updating Firmware");
      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->print("Downloading...", Color::LABEL);
      _downloadRowY = _arduino->getCursorY();

      _progressBarY = _downloadRowY + _arduino->charH() + _PROGRESS_BAR_MARGIN;
      _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, _arduino->width() - 2 * _PROGRESS_BAR_MARGIN, _PROGRESS_BAR_HEIGHT, Color::DARKGRAY);
   }
#endif

   WiFiClientSecure client;
   client.setInsecure();

   // Without an explicit timeout, a stalled TCP connect or TLS handshake can block here
   // indefinitely (no bytes ever downloaded, so _onUpdateProgress never even fires) with
   // nothing but silence on Serial and no visible display update -- indistinguishable from
   // a genuine hang. Fail fast instead so a connectivity/handshake problem surfaces as a
   // normal failure result.
   client.setTimeout(_CONNECT_TIMEOUT_MS);
   client.setHandshakeTimeout(_CONNECT_TIMEOUT_MS / 1000);

   // httpUpdate.update() drives both the HTTP download and the flash write from inside a
   // single blocking call, so a display redraw triggered by its progress callback can land
   // at the exact moment the flash caches on both cores are disabled for a Update.write()
   // call underneath it - corrupting whatever SPI transaction was mid-flight. Driving the
   // download manually here instead means every Update.write() call (and the brief cache
   // disable it causes) happens at a point we control, strictly *between* display draws,
   // never during one.
   std::string reason;
   bool ok = false;

   HTTPClient http;
   http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
   http.setTimeout(_CONNECT_TIMEOUT_MS);

   // Flash writes during the update briefly halt execution on the *other* core (both
   // cores' flash caches must be disabled together while writing), which can starve that
   // core's idle task long enough to miss its task watchdog window and panic-reset the
   // device mid-download. Fully deinitializing the task watchdog timer for the duration
   // of the download (instead of just unsubscribing the idle tasks via
   // disableCore0WDT()/disableCore1WDT()) avoids that panic *and* stops the watchdog's
   // own timer, so its idle-task hook has nothing left to fire on and no longer spams
   // Serial with "task not found" errors while disabled. Re-initialized below once the
   // download returns (a successful update instead reboots directly).
   esp_task_wdt_deinit();

   if (!http.begin(client, _firmwareUrl.c_str()))
   {
      reason = "HTTP begin() failed";
   }
   else
   {
      int httpCode = http.GET();
      if (httpCode != HTTP_CODE_OK)
      {
         reason = "HTTP GET failed, code: " + std::to_string(httpCode);
      }
      else
      {
         int contentLength = http.getSize();
         if (contentLength <= 0)
         {
            reason = "Invalid content length";
         }
         else if (!Update.begin(contentLength))
         {
            reason = "Update.begin() failed: " + std::string(Update.errorString());
         }
         else
         {
            WiFiClient* stream = http.getStreamPtr();
            constexpr size_t CHUNK_SIZE = 4096;

            // Allocated on the heap rather than as a stack array: this method's task stack
            // (the default Arduino loop task, sized for typical sketch code) doesn't have
            // enough headroom for a 4KB local array on top of its other locals/call frames,
            // and blows the stack guard the moment this function is entered - before any
            // code in the function body, including the display draw calls, ever runs.
            std::unique_ptr<uint8_t[]> buffer(new uint8_t[CHUNK_SIZE]);
            int written = 0;
            uint32_t lastReadTimeMs = millis();

            while (http.connected() && written < contentLength)
            {
               size_t available = stream->available();
               if (available == 0)
               {
                  if (millis() - lastReadTimeMs >= _STALL_TIMEOUT_MS)
                  {
                     reason = "Download stalled: no data received for " + std::to_string(_STALL_TIMEOUT_MS / 1000) + "s";
                     break;
                  }
                  delay(1);
                  continue;
               }

               size_t toRead = std::min(available, CHUNK_SIZE);
               size_t readBytes = stream->readBytes(buffer.get(), toRead);
               if (readBytes == 0)
               {
                  continue;
               }
               lastReadTimeMs = millis();

               // Update.write() briefly disables both cores' flash caches while it writes
               // this chunk. No display drawing happens on either side of this call within
               // the same loop iteration, so that cache-disable window never overlaps with
               // an in-flight SPI transaction.
               if (Update.write(buffer.get(), readBytes) != readBytes)
               {
                  reason = "Update.write() failed: " + std::string(Update.errorString());
                  break;
               }

               written += readBytes;
               _onUpdateProgress(written, contentLength);
            }

            if (reason.empty())
            {
               if (written != contentLength)
               {
                  reason = "Download incomplete: " + std::to_string(written) + " of " + std::to_string(contentLength) + " bytes";
               }
               else if (!Update.end(true))
               {
                  reason = "Update.end() failed: " + std::string(Update.errorString());
               }
               else
               {
                  ok = true;
               }
            }
         }
      }
      http.end();
   }

   // Restore the task watchdog torn down by esp_task_wdt_deinit() above, with the same
   // config Arduino-ESP32 uses at boot (idle tasks on both cores, panic on timeout).
   esp_task_wdt_config_t wdtConfig
   {
      .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
      .idle_core_mask = (1 << 0) | (1 << 1),
      .trigger_panic = true,
   };
   esp_task_wdt_init(&wdtConfig);

   if (ok)
   {
      _log("OTAUpdater: update OK, restarting");
#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_arduino != nullptr)
      {
         _arduino->println("\n\nRestarting...", Color::LABEL);
      }
#endif
      if (_handler != nullptr)
      {
         _handler->onUpdateSucceeded(_availableVersion.c_str());
      }
      ESP.restart();
   }
   else
   {
      _log((std::string("OTAUpdater: update failed: ") + reason + ", url: " + _firmwareUrl).c_str());
#ifdef ARDUINO_DISPLAY_SUPPORTED
      if (_arduino != nullptr)
      {
         _arduino->println();
         _arduino->println("Update failed", Color::RED);
         _arduino->println(reason.c_str(), Color::RED);
         delay(_RESULT_DELAY_MS);
      }
#endif
      if (_handler != nullptr)
      {
         _handler->onUpdateFailed(_availableVersion.c_str(), reason.c_str());
      }
   }
}
