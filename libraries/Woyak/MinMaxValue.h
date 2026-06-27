#pragma once

#include <Arduino.h>
#include "ValueStore.h"

class MinMaxValue
{
private:
   IValueStore* _min;
   IValueStore* _max;
   IValueStore* _value;

public:
   MinMaxValue(IValueStore* value = nullptr, IValueStore* min = nullptr, IValueStore* max = nullptr)
   {
      if (value == nullptr)
      {
         value = new ValueStoreSimple();
      }
      if (min == nullptr)
      {
         min = new ValueStoreSimple();
      }
      if (max == nullptr)
      {
         max = new ValueStoreSimple();
      }

      _value = value;
      _min = min;
      _max = max;
   }

   virtual ~MinMaxValue()
   {
      delete _value;
      delete _min;
      delete _max;
   }

   void resetMinMax()
   {
      // reset by using the current value as the new min/max
      float value = _value->get();
      _min->set(value);
      _max->set(value);
   }

   void setValue(float value)
   {
      // store the value
      _value->set(value);

      // get the value - store mechanism may manipulate it, e.g. average of last 5 values
      value = _value->get();

      // update min/max values
      float minValue = _min->get();
      if (isnan(minValue))
      {
         _min->set(value);
      }
      else
      {
         _min->set(min(value, minValue));
      }

      float maxValue = _max->get();
      if (isnan(maxValue))
      {
         _max->set(value);
      }
      else
      {
         _max->set(max(value, maxValue));
      }
   }

   float getMin()
   {
      return _min->get();
   }

   float getMax()
   {
      return _max->get();
   }

   float getValue()
   {
      return _value->get();
   }
};
