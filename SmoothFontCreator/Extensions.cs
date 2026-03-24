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
      TextFormatFlags flags = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.SingleLine | TextFormatFlags.NoClipping;
      TextRenderer.DrawText(graphics, str, font, pt, fgColor, bgColor, flags);
   }
   public static void DrawPreciseString(this Graphics graphics, char c, Font font, Point pt, Color fgColor, Color bgColor)
   {
      graphics.DrawPreciseString(c.ToString(), font, pt, fgColor, bgColor);
   }

   public static GdiMetrics GetGdiMetrics(this Font font)
   {
      return new GdiMetrics(font);
   }

   public static String ToHex(this int i)
   {
      if (i < 16)
      {
         return "0x0" + i.ToString("X");
      }
      else
      {
         return "0x" + i.ToString("X");
      }
   }

   public static String ToHex(this char c)
   {
      return ((int)c).ToHex();
   }

   public static String ToHex(this byte b)
   {
      return ((int)b).ToHex();
   }

   /*
   public static ObservedMetrics GetObservedMetrics(this Font font, char c)
   {
      return new ObservedMetrics(font, c);
   }
   */
}