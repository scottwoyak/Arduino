
namespace SmoothFontCreator;

public enum LabelAlignment
{
   Left,
   Right,
};


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

   public static void DrawLabel(this Graphics graphics, string str, Font font, PointF pt, Color fgColor, Color bgColor, LabelAlignment alignment= LabelAlignment.Left)
   {
      SizeF size = graphics.MeasureString(str, font);

      pt.Y -= size.Height / 2;
      if (alignment == LabelAlignment.Right)
      {
         pt.X -= size.Width;
      }

      graphics.FillRectangle(new SolidBrush(bgColor), new RectangleF(pt, size));
      graphics.DrawString(str, font, new SolidBrush(fgColor), pt);
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