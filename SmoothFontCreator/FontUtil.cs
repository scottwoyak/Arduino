

namespace SmoothFontCreator;

public class FontUtil
{
   public static float LineSpacingPxToFontSizePt(FontFamily fontFamily, FontStyle fontStyle, Graphics graphics, float desiredLineSpacingPx)
   {
      using Font font = new( fontFamily, 100, fontStyle, GraphicsUnit.Pixel);

      float heightPx = font.GetHeight(graphics);
      float heightEm = font.FontFamily.GetEmHeight(FontStyle.Regular);
      float lineSpacingEm = font.FontFamily.GetLineSpacing(FontStyle.Regular);
      float lineSpacingPx = (heightPx / heightEm) * lineSpacingEm;

      return desiredLineSpacingPx * (font.SizeInPoints / lineSpacingPx);
   }

   public static float LineSpacingPxToFontSizePx(FontFamily fontFamily, FontStyle fontStyle, Graphics graphics, float desiredLineSpacingPx)
   {
      using Font font = new(fontFamily, 100, fontStyle, GraphicsUnit.Pixel);

      float heightPx = font.GetHeight(graphics);
      float heightEm = font.FontFamily.GetEmHeight(FontStyle.Regular);
      float lineSpacingEm = font.FontFamily.GetLineSpacing(FontStyle.Regular);
      float lineSpacingPx = (heightPx / heightEm) * lineSpacingEm;

      return desiredLineSpacingPx * (font.Size / lineSpacingPx);
   }
   public static float LineSpacingPxToFontSizePx2(FontFamily fontFamily, FontStyle fontStyle, Graphics graphics, float desiredLineSpacingPx)
   {
      using Font font = new(fontFamily, 100, fontStyle, GraphicsUnit.Pixel);

      float heightPx = font.GetHeight(graphics);
      return desiredLineSpacingPx * (100 / heightPx);
   }

   /*
   public static void DrawText(Graphics graphics, string str, Font font, Point pt, Color fgColor, Color bgColor)
   {
      graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
      TextRenderer.DrawText(graphics, str, font, pt, fgColor, bgColor, TextFormatFlags.NoPadding|TextFormatFlags.NoPrefix);
   }
   */
}
