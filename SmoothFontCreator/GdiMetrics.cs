namespace SmoothFontCreator;

public class GdiMetrics
{
   public float SizePt { get; }
   public float SizePx { get; }
   public float HeightPx { get; }
   public float WidthPx { get; }
   public float AscentPx { get; }
   public float DescentPx { get; }
   public float LineSpacingPx { get; }
   public float BaselinePx { get; }
   public float BottomPx { get; }

   public GdiMetrics(Font font)
   {
      SizePt = font.SizeInPoints;
      SizePx = font.GetHeight();

      float heightEm = font.FontFamily.GetEmHeight(FontStyle.Regular);
      float ascentEm = font.FontFamily.GetCellAscent(FontStyle.Regular);
      float descentEm = font.FontFamily.GetCellDescent(FontStyle.Regular);
      float lineSpacingEm = font.FontFamily.GetLineSpacing(FontStyle.Regular);

      HeightPx = font.GetHeight();
      WidthPx = TextRenderer.MeasureText("0", font, new Size(1000, 1000), TextFormatFlags.NoPadding).Width;
      AscentPx = (ascentEm / lineSpacingEm) * HeightPx;
      DescentPx = (descentEm / lineSpacingEm) * HeightPx;
      LineSpacingPx = (HeightPx / heightEm) * lineSpacingEm;

      BaselinePx = AscentPx;
      BottomPx = AscentPx + DescentPx;
   }
}
