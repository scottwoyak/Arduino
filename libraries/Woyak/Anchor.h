#pragma once

///
/// <summary>
/// Identifies which point of a rectangular region a position passed to a setPosition()
/// method refers to. Defaults to TOP_LEFT, matching the original top-left-only behavior
/// of most setPosition() overloads.
/// </summary>
///
enum class Anchor
{
   TOP_LEFT,
   TOP_RIGHT,
   BOTTOM_LEFT,
   BOTTOM_RIGHT,
   CENTER
};
