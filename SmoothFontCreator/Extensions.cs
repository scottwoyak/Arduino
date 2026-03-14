using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;

namespace SmoothFontCreator;

public static class GraphicsExtensions
{
   public static void DrawPreciseString(this Graphics graphics, string str, Font font, Point pt, Color fgColor, Color bgColor)
   {
      graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
      TextRenderer.DrawText(graphics, str, font, pt, fgColor, bgColor, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
   }
   public static void DrawPreciseString(this Graphics graphics, char c, Font font, Point pt, Color fgColor, Color bgColor)
   {
      graphics.DrawPreciseString(c.ToString(), font, pt, fgColor, bgColor);
   }

   public static GdiMetrics GetGdiMetrics(this Font font)
   {
      return new GdiMetrics(font);
   }

   /*
   public static ObservedMetrics GetObservedMetrics(this Font font, char c)
   {
      return new ObservedMetrics(font, c);
   }
   */
}