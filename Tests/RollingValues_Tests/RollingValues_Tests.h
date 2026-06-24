#pragma once

#include <AUnit.h>
#include "RollingValues.h"

test(rollingValuesShouldReturnFalseWhenSizeIsZero)
{
   RollingValues values(0);
   float removed = 123.0f;

   bool hasRemoved = values.set(42.0f, &removed);

   assertFalse(hasRemoved);
   assertEqual(123.0f, removed);
}

test(rollingValuesShouldNotReportRemovedBeforeFirstWrap)
{
   RollingValues values(3);
   float removed = 999.0f;

   assertFalse(values.set(1.0f, &removed));
   assertEqual(999.0f, removed);

   assertFalse(values.set(2.0f, &removed));
   assertEqual(999.0f, removed);

   assertFalse(values.set(3.0f, &removed));
   assertEqual(999.0f, removed);

   assertNear(3.0f, values.get(0), 0.0001f);
   assertNear(2.0f, values.get(1), 0.0001f);
   assertNear(1.0f, values.get(2), 0.0001f);
}

test(rollingValuesShouldReportRemovedAfterWrap)
{
   RollingValues values(3);
   float removed = NAN;

   values.set(1.0f);
   values.set(2.0f);
   values.set(3.0f);

   bool hasRemoved = values.set(4.0f, &removed);

   assertTrue(hasRemoved);
   assertNear(1.0f, removed, 0.0001f);
   assertNear(4.0f, values.get(0), 0.0001f);
   assertNear(3.0f, values.get(1), 0.0001f);
   assertNear(2.0f, values.get(2), 0.0001f);
}

test(rollingValuesShouldAllowIgnoringRemovedPointer)
{
   RollingValues values(2);

   assertFalse(values.set(10.0f));
   assertFalse(values.set(20.0f));
   assertTrue(values.set(30.0f));

   assertNear(30.0f, values.get(0), 0.0001f);
   assertNear(20.0f, values.get(1), 0.0001f);
}

test(rollingValuesShouldResetToInitialValues)
{
   RollingValues values(3, 7.5f);

   values.set(1.0f);
   values.set(2.0f);

   values.reset();

   assertEqual((size_t)0, values.count());
   assertEqual((size_t)3, values.size());
   assertNear(7.5f, values.get(0), 0.0001f);
   assertNear(7.5f, values.get(1), 0.0001f);
   assertNear(7.5f, values.get(2), 0.0001f);
}

test(rollingValuesShouldResizeAndClearState)
{
   RollingValues values(2, -1.0f);

   values.set(3.0f);
   values.set(4.0f);

   values.resize(4);

   assertEqual((size_t)0, values.count());
   assertEqual((size_t)4, values.size());
   assertNear(-1.0f, values.get(0), 0.0001f);
   assertNear(-1.0f, values.get(3), 0.0001f);

   assertFalse(values.set(8.0f));
   assertNear(8.0f, values.get(0), 0.0001f);
   assertNear(-1.0f, values.get(1), 0.0001f);
}
