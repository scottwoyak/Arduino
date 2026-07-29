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
   TOP_CENTER,
   BOTTOM_LEFT,
   BOTTOM_RIGHT,
   BOTTOM_CENTER,
   MIDDLE_LEFT,
   MIDDLE_RIGHT,
   CENTER
};

///
/// <summary>
/// Identifies which vertical point of a rectangular region a y coordinate passed to a
/// setPosition() method refers to. Used by APIs that don't have a separate horizontal
/// alignment concept, or that already control horizontal placement some other way (e.g.
/// DisplayValue, which controls horizontal placement via its own Alignment field).
/// Defaults to TOP, matching the original top-only behavior of most setPosition()
/// overloads.
/// </summary>
///
enum class VerticalAnchor
{
   TOP,
   BOTTOM,
   MIDDLE
};
