#pragma once

#include <AUnit.h>
#include "StdDev.h"

// Test 1: Verify the default uninitialized state outputs NAN
test(StdDevTest, uninitializedStateReturnsNan)
{
   StdDev stdDev;

   assertEqual(stdDev.count(), (size_t)0);
   assertTrue(isnan(stdDev.mean()));
   assertTrue(isnan(stdDev.get()));
}

// Test 2: Verify statistics after adding a single element
test(StdDevTest, singleElementStats)
{
   StdDev stdDev;
   stdDev.add(42.5f);

   assertEqual(stdDev.count(), (size_t)1);
   assertEqual(stdDev.mean(), 42.5f);
   assertEqual(stdDev.get(), 0.0f); // Standard deviation of a single point is 0
}

// Test 3: Verify math against a known sample dataset
// Population dataset: {2.0, 4.0, 4.0, 4.0, 5.5, 5.7, 9.0, 13.0}
// Count: 8 | Mean: 5.9f | Population SD: ≈3.28367
test(StdDevTest, populationCalculationAccuracy)
{
   StdDev stdDev;

   stdDev.add(2.0f);
   stdDev.add(4.0f);
   stdDev.add(4.0f);
   stdDev.add(4.0f);
   stdDev.add(5.5f);
   stdDev.add(5.7f);
   stdDev.add(9.0f);
   stdDev.add(13.0f);

   assertEqual(stdDev.count(), (size_t)8);
   assertNear(stdDev.mean(), 5.9f, 0.0001f);
   assertNear(stdDev.get(), 3.28367f, 0.0001f);
}

// Test 4: Verify the Welford Downdate (remove function) works perfectly
test(StdDevTest, removingDataPointUpdatesStats)
{
   StdDev stdDev;

   stdDev.add(10.0f);
   stdDev.add(20.0f);
   stdDev.add(30.0f); // Mean: 20, Pop SD: 8.16496f

   // Remove the outlier (30.0)
   bool success = stdDev.remove(30.0f);

   assertTrue(success);
   assertEqual(stdDev.count(), (size_t)2);
   assertNear(stdDev.mean(), 15.0f, 0.0001f); // Remaining: {10, 20}
   assertNear(stdDev.get(), 5.0f, 0.0001f);    // Pop SD of {10, 20} is 5.0
}

// Test 5: Verify safety constraints when calling remove on empty or small classes
test(StdDevTest, edgeCaseRemovals)
{
   StdDev stdDev;

   // Cannot remove from an empty stdDev
   assertFalse(stdDev.remove(10.0f));

   // Removing the last element drops it clean back to uninitialized state
   stdDev.add(10.0f);
   assertTrue(stdDev.remove(10.0f));
   assertEqual(stdDev.count(), (size_t)0);
   assertTrue(isnan(stdDev.mean()));
   assertTrue(isnan(stdDev.get()));
}

// Test 6: Verify the reset function clears all data
test(StdDevTest, resetClearsState)
{
   StdDev stdDev;
   stdDev.add(5.0f);
   stdDev.add(10.0f);

   stdDev.reset();

   assertEqual(stdDev.count(), (size_t)0);
   assertTrue(isnan(stdDev.mean()));
   assertTrue(isnan(stdDev.get()));
}

// Test 7: Verify requiredSamples() handles valid and invalid inputs
test(StdDevTest, requiredSamplesCalculatesAndValidatesInputs)
{
   StdDev stdDev;
   stdDev.add(0.0f);
   stdDev.add(1.0f);

   assertEqual((size_t)4, stdDev.requiredSamples(0.5f, 2.0f));
   assertEqual((size_t)0, stdDev.requiredSamples(0.0f, 2.0f));
   assertEqual((size_t)0, stdDev.requiredSamples(0.5f, 0.0f));
}
