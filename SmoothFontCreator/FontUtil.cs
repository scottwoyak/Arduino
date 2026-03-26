

namespace SmoothFontCreator;

public class FontUtil
{
   //
   // Measuring windows fonts is a bit of a mystery. If you print a string, the background (cell)
   // is pretty close to the value of the Ascent + Descent, but can be a few percent larger. That
   // actual font size requested is an unknown actual value. This function tells you what the
   // needed font size is to create a font with the desired cell height.
   public static float GetFontSizePxForCellHeightPx(
      FontFamily fontFamily, 
      FontStyle fontStyle, 
      float desiredCellHeightPx)
   {
      // create a dummy font
      float dummyFontSize = 100; // doesn't reall matter what value is used
      using Font font = new(fontFamily, dummyFontSize, fontStyle, GraphicsUnit.Pixel);

      // see what GDI reports to us
      GdiMetrics gdiMetrics = new(font);
      float approxCellHeightPx = gdiMetrics.AscentPx + gdiMetrics.DescentPx;

      // return the scaled value
      return desiredCellHeightPx * (dummyFontSize / font.GetHeight());
   }
}
