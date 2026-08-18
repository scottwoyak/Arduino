#pragma once

#include <algorithm>
#include <vector>

#include "Status.h"

///
/// <summary>
/// Combines multiple IStatus indicators (e.g. an onboard NeoPixel plus an external RGB
/// LED) so they all reflect the same status simultaneously. Useful when one indicator
/// may not always be present/plugged in.
/// </summary>
///
class MultiStatus : public IStatus
{
private:
   std::vector<IStatus*> _statuses;

public:
   ///
   /// <summary>
   /// Initializes a composite status indicator from zero or more underlying indicators.
   /// Additional indicators can be added later via addStatus().
   /// </summary>
   /// <param name="status1">First underlying status indicator, or nullptr for none.</param>
   /// <param name="status2">Second underlying status indicator, or nullptr for none.</param>
   ///
   MultiStatus(IStatus* status1 = nullptr, IStatus* status2 = nullptr)
   {
      addStatus(status1);
      addStatus(status2);
   }

   ///
   /// <summary>
   /// Adds an underlying status indicator to the composite.
   /// </summary>
   /// <param name="status">The status indicator to add; ignored if nullptr.</param>
   ///
   void addStatus(IStatus* status)
   {
      if (status != nullptr)
      {
         _statuses.push_back(status);
      }
   }

   ///
   /// <summary>
   /// Removes an underlying status indicator from the composite, if present.
   /// </summary>
   /// <param name="status">The status indicator to remove.</param>
   ///
   void removeStatus(IStatus* status)
   {
      _statuses.erase(std::remove(_statuses.begin(), _statuses.end(), status), _statuses.end());
   }

   ///
   /// <summary>
   /// Initializes all underlying status indicators.
   /// </summary>
   ///
   void begin() override
   {
      for (IStatus* status : _statuses)
      {
         status->begin();
      }
   }

   ///
   /// <summary>
   /// Updates all underlying status indicators to reflect the specified status.
   /// </summary>
   /// <param name="status">The status value to display.</param>
   ///
   void setStatus(Status status) override
   {
      for (IStatus* s : _statuses)
      {
         s->setStatus(status);
      }
   }
};

