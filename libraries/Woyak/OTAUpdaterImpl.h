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

   Serial.println(MESSAGE);

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

   Util::setHaltReason(MESSAGE);
   Util::reset();
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

      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->setCursorY(_downloadRowY);
      _arduino->printR((float)percent, _percentFormat, Color::VALUE);

      int16_t barWidth = _arduino->width() - 2 * _PROGRESS_BAR_MARGIN;
      int16_t fillWidth = barWidth * percent / 100;
      _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, fillWidth, _PROGRESS_BAR_HEIGHT, Color::LIME);
   }
#endif
}

inline void OTAUpdater::_performUpdate()
{
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
   // normal HTTP_UPDATE_FAILED result.
   client.setTimeout(_CONNECT_TIMEOUT_MS);
   client.setHandshakeTimeout(_CONNECT_TIMEOUT_MS / 1000);

   _active = this;

   Serial.printf("OTAUpdater: free heap before update: %lu bytes, largest block: %lu bytes\n",
      ESP.getFreeHeap(), ESP.getMaxAllocHeap());

   // Always register, even without a display, so _onUpdateProgress() can keep yielding
   // while httpUpdate.update() blocks below.
   httpUpdate.onProgress(_onUpdateProgressHandler);

   httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
   httpUpdate.rebootOnUpdate(false);

   // httpUpdate.update() blocks this task for the entire download. That alone is fine -
   // other tasks/cores can still run - but on dual-core boards, flash writes during the
   // update briefly halt execution on the *other* core (both cores' flash caches must be
   // disabled together while writing), which can starve that core's idle task long enough
   // to miss its task watchdog window and panic-reset the device mid-download. Arduino-
   // ESP32 provides disableCore0WDT()/disableCore1WDT() specifically for this: they remove
   // the idle tasks from the TWDT for the duration of the download, and are re-enabled
   // below once it returns (a successful update instead reboots directly). Their idle
   // hooks keep calling esp_task_wdt_reset() every idle tick regardless of registration
   // state, so each of those calls logs a "task not found" error to Serial while
   // disabled. That log comes from ESP_EARLY_LOGE, which bypasses runtime log-level
   // filtering, so it can't be silenced here - noisy, but not a functional problem.
   disableCore0WDT();
   disableCore1WDT();

   t_httpUpdate_return result = httpUpdate.update(client, _firmwareUrl.c_str());

   enableCore0WDT();
   enableCore1WDT();

   switch (result)
   {
      case HTTP_UPDATE_FAILED:
      {
         std::string reason = httpUpdate.getLastErrorString().c_str();
         Serial.printf("OTAUpdater: update failed: %s, url: %s\n", reason.c_str(), _firmwareUrl.c_str());
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
         break;
      }

      case HTTP_UPDATE_NO_UPDATES:
         Serial.println("OTAUpdater: no update available");
#ifdef ARDUINO_DISPLAY_SUPPORTED
         if (_arduino != nullptr)
         {
            _arduino->println();
            _arduino->println("No update available", Color::RED);
            delay(_RESULT_DELAY_MS);
         }
#endif
         break;

      case HTTP_UPDATE_OK:
         Serial.println("OTAUpdater: update OK, restarting");
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
         break;
   }
}
