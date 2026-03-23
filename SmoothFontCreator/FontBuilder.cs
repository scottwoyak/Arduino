
using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public class FontBuilder
{
   //private Bitmap _largeBitmap = new Bitmap(2048, 2048);
   private Bitmap _largeBitmap = new Bitmap(1024, 1024);
   private Graphics _largeGraphics;
   private ObservedMetrics _allCharsMetrics;
   private Dictionary<char, ObservedMetrics> _charMetrics = [];

   public Bitmap LargeBitmap => _largeBitmap;

   public FontFamily FontFamily { get; private set; } = new FontFamily("Arial");

   public FontStyle FontStyle { get; private set; } = FontStyle.Regular;

   private FontBuilder(string fontFamilyName, FontStyle fontStyle)
   {
      _largeGraphics = Graphics.FromImage(_largeBitmap);

      FontFamily = new FontFamily(fontFamilyName);
      FontStyle = fontStyle;

      Stopwatch sw = Stopwatch.StartNew();

      int testSize = 1*1024; // TODO does this need to be this large?
      using Bitmap bitmap = new(testSize, testSize);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(FontFamily, bitmapGraphics, testSize));

      Font font = new Font(
         FontFamily,
         fontSizePx,
         fontStyle,
         GraphicsUnit.Pixel);
      _allCharsMetrics = new(font);

      float heightPx = font.GetHeight();
      float widthPx;
      Point pt;

      // TODO optimize this the way we did in the GUI by drawing backgrounds and then chars

      // then draw all the charcters and measure again
      for (char c = (char)0x21; c < (char)0x7F; c++)
      {
         //Debug.WriteLine($"Building '{c}' {c.ToHex()}");
         bitmapGraphics.Clear(Color.Transparent);

         widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
         pt = new((int)(bitmap.Width - widthPx) / 2, 0);
         bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
         ObservedMetrics charMetrics = new(bitmap, font);
         if (c == 'a')
         {

         }
         _charMetrics[c] = charMetrics;

         _allCharsMetrics.Expand(charMetrics);
      }

      /*
      Debug.WriteLine($"Font Metrics Collected in {sw.ElapsedMilliseconds}ms");
      Debug.WriteLine($"   Requested fontSizePx: {fontSizePx}");
      Debug.WriteLine($"   font.Size: {font.Size}");
      Debug.WriteLine($"   font.SizeInPoints: {font.SizeInPoints}");
      Debug.WriteLine($"   font.Height: {font.Height}");
      Debug.WriteLine($"   font.GetHeight: {font.GetHeight()}");
      Debug.WriteLine($"   Observed Metrics for all chars:");
      Debug.WriteLine($"      CharRect: {_allCharsObservedMetrics.CharRectString}");
      Debug.WriteLine($"      CellRect: {_allCharsObservedMetrics.CellRectString}");
      */
   }

   public Bitmap CreateGlyph(uint charHeightPx, char c)
   {
      double aspectRatio = _charMetrics[c].CellWidth / _allCharsMetrics.CharHeight;

      //
      // Step 1 - draw the character to a large bitmap
      //

      // create a font such that all characters fit the height of the large bitmap
      double largeFontSizePx = _allCharsMetrics.RealPxToFontSizePx(_largeBitmap.Height);

      Font font = new Font(
         FontFamily,
         (float)largeFontSizePx,
         FontStyle,
         GraphicsUnit.Pixel);

      ObservedMetrics largeCharMetrics = _allCharsMetrics.WithCharHeight(_largeBitmap.Height);
      double largeTopMarginPx = largeCharMetrics.TopMargin;

      // draw the character
      _largeGraphics.Clear(Color.Transparent);

      Point pt = new(0, (int)-largeTopMarginPx); // offset the top so that the tallest character is draw on the first row
      _largeGraphics.DrawPreciseString(c, font, pt, Color.White, Color.Transparent);

      //
      // Scale the larger bitmap to the smaller one
      //
      int smallHeightPx = (int)charHeightPx;
      int smallWidthPx = (int)Math.Ceiling(aspectRatio * smallHeightPx);
      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      //smallGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
      smallGraphics.InterpolationMode = InterpolationMode.Bilinear;
      //smallGraphics.InterpolationMode = InterpolationMode.Low;
      Rectangle srcRect = new(0, 0, (int)Math.Ceiling(aspectRatio * _largeBitmap.Height), _largeBitmap.Height);
      Rectangle dstRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);

      //smallGraphics.Clear(Color.Pink); // so we can detect problems - all pink should get covered up
      smallGraphics.DrawImage(_largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);

      return smallBitmap;
   }

   public VLWFont CreateFont(uint charHeightPx, bool monospaced)
   {
      VLWFont vlw = new();

      ObservedMetrics allCharsMetrics = _allCharsMetrics.WithCharHeight(charHeightPx);
      double scaleFactor = allCharsMetrics.CellHeight / _allCharsMetrics.CellHeight;
      ObservedMetrics oMetrics = _charMetrics['o'].WithScaleFactor(scaleFactor);

      vlw.Ascent = 0;
      //vlw.Ascent = (uint)(oMetrics.CharBottom - allCharsMetrics.CharTop);
      vlw.Descent = (int)(charHeightPx - vlw.Ascent);
      vlw.FontSizePx = charHeightPx;

      for (char c = (char)0x21; c < 0x7F; c++)
      {
         ObservedMetrics thisCharMetrics = _charMetrics[c].WithScaleFactor(scaleFactor);

         VLWGlyph glyph = new();
         glyph.uChar = c;
         glyph.Bitmap = CreateGlyph(charHeightPx, c);


         vlw.Glyphs.Add(c, glyph);

         if (monospaced)
         {
            glyph.gxAdvance = (int)Math.Ceiling(allCharsMetrics.CellWidth);

            glyph.gdX = (int)((allCharsMetrics.CellWidth - thisCharMetrics.CellWidth) / 2.0);
            glyph.gdY = 0;
            //glyph.gdY = (int)Math.Ceiling(allCharsMetrics.TopMargin + allCharsMetrics.CellTop);
            glyph.Width = glyph.Bitmap.Width;
            glyph.Height = glyph.Bitmap.Height;
         }
         else
         {
            glyph.gdX = 0; // (int)Math.Ceiling(thisCharMetrics.LeftMargin);
            glyph.gdY = 0; // (int)Math.Ceiling(thisCharMetrics.TopMargin);
            glyph.Width = (int)Math.Floor(thisCharMetrics.CharWidth);
            glyph.Height = (int)Math.Ceiling(thisCharMetrics.CharHeight);
            glyph.gxAdvance = (int)Math.Floor(thisCharMetrics.CellWidth);
            glyph.gxAdvance = glyph.Bitmap.Width; // (int)Math.Floor(thisCharMetrics.CellWidth);
         }
      }

      return vlw;
   }


   public static FontBuilder ForFontFamily(string fontFamily, FontStyle fontStyle)
   {
      return new FontBuilder(fontFamily, fontStyle);
   }
}
