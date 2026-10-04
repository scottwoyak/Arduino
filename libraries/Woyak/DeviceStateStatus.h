#pragma once

#include "DeviceServerClient.h"
#include "Status.h"

///
/// <summary>
/// Status indicator that reports the device state to the DeviceServer (see
/// DeviceServerClient::setState()). Add it to a MultiStatus alongside the LED indicators
/// so every status change is also sent to the server.
/// </summary>
///
class DeviceStateStatus : public IStatus
{
private:
   ///
   /// <summary>
   /// Returns the label sent to the server for a status value.
   /// </summary>
   /// <param name="status">Status value to describe</param>
   /// <returns>Status label string</returns>
   ///
   static const char* _statusString(Status status)
   {
      switch (status)
      {
      case Status::STARTED:         return "STARTED";
      case Status::WIFI_CONNECTING: return "WIFI_CONNECTING";
      case Status::WEB_CONNECTING:  return "WEB_CONNECTING";
      case Status::RUNNING:           return "RUNNING";
      case Status::FAILED:          return "FAILED";
      case Status::UPDATING:        return "UPDATING";
      case Status::RESTARTING:      return "RESTARTING";
      default:                      return "UNKNOWN";
      }
   }

public:
   ///
   /// <summary>
   /// Does nothing; there is no hardware to initialize.
   /// </summary>
   ///
   void begin() override
   {
   }

   ///
   /// <summary>
   /// Does nothing; turning indicators off is not a device state.
   /// </summary>
   ///
   void off() override
   {
   }

   ///
   /// <summary>
   /// Reports the status to the DeviceServer. Delivered immediately if connected, otherwise
   /// with the next handshake.
   /// </summary>
   /// <param name="status">The status value to report.</param>
   ///
   void setStatus(Status status) override
   {
      DeviceServerClient::setState(_statusString(status));
   }
};
