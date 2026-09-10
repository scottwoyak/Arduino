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
      _arduino->printHeader("OTA Update");
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

inline void OTAUpdater::_onUpdateProgress(int current, int total)
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

inline void OTAUpdater::_performUpdate()
{
#ifdef ARDUINO_DISPLAY_SUPPORTED
   if (_arduino != nullptr)
   {
      _arduino->printHeader("Updating Firmware");
      _arduino->setTextSize(_TEXT_SIZE);
      _arduino->print("Downloading...", Color::LABEL);
      _downloadRowY = _arduino->getCursorY();

      _progressBarY = _downloadRowY + _arduino->charH() + _PROGRESS_BAR_MARGIN;
      _arduino->fillRect(_PROGRESS_BAR_MARGIN, _progressBarY, _arduino->width() - 2 * _PROGRESS_BAR_MARGIN, _PROGRESS_BAR_HEIGHT, Color::DARKGRAY);
   }
#endif

   WiFiClientSecure client;
   client.setInsecure();

   _active = this;

#ifdef ARDUINO_DISPLAY_SUPPORTED
   if (_arduino != nullptr)
   {
      httpUpdate.onProgress(_onUpdateProgressHandler);
   }
#endif
   httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
   httpUpdate.rebootOnUpdate(false);

   t_httpUpdate_return result = httpUpdate.update(client, _firmwareUrl.c_str());

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
