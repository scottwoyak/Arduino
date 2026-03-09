namespace SmoothFontCreator;

class GdiMetrics
{
   public float Height;
   public float Width;
   public float Ascent;
   public float Descent;
   public float LineSpacing;
   public float Top;
   public float Baseline;
   public float Bottom;

   public GdiMetrics(Font font, Graphics g)
   {
      float heightEm = font.FontFamily.GetEmHeight(FontStyle.Regular);
      float ascentEm = font.FontFamily.GetCellAscent(FontStyle.Regular);
      float descentEm = font.FontFamily.GetCellDescent(FontStyle.Regular);
      float lineSpacingEm = font.FontFamily.GetLineSpacing(FontStyle.Regular);

      Height = font.GetHeight(g);
      Width = TextRenderer.MeasureText("0", font, new Size(1000, 1000), TextFormatFlags.NoPadding).Width;
      Ascent = (ascentEm / lineSpacingEm) * Height;
      Descent = (descentEm / lineSpacingEm) * Height;
      LineSpacing = (Height / heightEm) * lineSpacingEm;

      Top = Height - font.Size;
      Baseline = Ascent;
      Bottom = Ascent + Descent;
   }
}
