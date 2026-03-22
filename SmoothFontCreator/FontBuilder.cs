
using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public class FontBuilder
{
   private Bitmap _largeBitmap = new Bitmap(1024, 1024);
   private Graphics _largeGraphics;
   private ObservedMetrics _allCharsObservedMetrics;
   private Dictionary<char, ObservedMetrics> _charMetrics = [];

   public FontFamily FontFamily { get; private set; } = new FontFamily("Arial");

   public FontStyle FontStyle { get; private set; } = FontStyle.Regular;

   private FontBuilder()
   {
      _largeGraphics = Graphics.FromImage(_largeBitmap);
   }

   private void SetFontFamily(string fontFamilyName, FontStyle fontStyle)
   {
      FontFamily = new FontFamily(fontFamilyName);
      FontStyle = fontStyle;

      Stopwatch sw = Stopwatch.StartNew();

      int testSize = 500; // TODO does this need to be this large?
      using Bitmap bitmap = new(testSize, testSize);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(FontFamily, bitmapGraphics, 0.9f * testSize));

      Font font = new Font(
         FontFamily,
         fontSizePx,
         FontStyle.Italic,
         GraphicsUnit.Pixel);
      _allCharsObservedMetrics = new(font);

      float heightPx = font.GetHeight();
      float widthPx;
      Point pt;

      // then draw all the charcters and measure again
      for (char c = (char)0x21; c < (char)0x7F; c++)
      {
         //Debug.WriteLine($"Building '{c}' {c.ToHex()}");
         bitmapGraphics.Clear(Color.Transparent);

         widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
         pt = new((int)(bitmap.Width - widthPx) / 2, (int)(bitmap.Height - heightPx) / 2);
         bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
         ObservedMetrics charMetrics = new(bitmap, font);
         _charMetrics[c] = charMetrics;

         _allCharsObservedMetrics.Expand(charMetrics);
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
      Stopwatch sw = Stopwatch.StartNew();
      Stopwatch sw2 = Stopwatch.StartNew();

      double aspectRatio = _charMetrics[c].CellWidth / _allCharsObservedMetrics.CharHeight;

      //
      // Step 1 - draw the character to a bitmap of height 2048
      //

      // create a font such that all characters fit the height of the large bitmap
      double largeFontSizePx = _allCharsObservedMetrics.RealPxToFontSizePx(_largeBitmap.Height);

      Font font = new Font(
         FontFamily,
         (float)largeFontSizePx,
         FontStyle,
         GraphicsUnit.Pixel);

      ObservedMetrics largeCharMetrics = _allCharsObservedMetrics.WithCharHeight(_largeBitmap.Height);
      double largeTopMarginPx = largeCharMetrics.TopMargin;

      //Debug.WriteLine($"Setup: {sw.Elapsed.TotalMilliseconds} ms"); sw.Restart();

      // draw the character
      _largeGraphics.Clear(Color.Transparent);
      //Debug.WriteLine($"Clear: {sw.Elapsed.TotalMilliseconds} ms"); sw.Restart();

      Point pt = new(0, (int)-largeTopMarginPx); // offset the top
      _largeGraphics.DrawPreciseString(c, font, pt, Color.White, Color.Transparent);

      //Debug.WriteLine($"Draw: {sw.Elapsed.TotalMilliseconds} ms"); sw.Restart();

      //
      // Scale the larger bitmap to the smaller one
      //
      int smallHeightPx = (int)charHeightPx;
      int smallWidthPx = (int)Math.Ceiling(aspectRatio * smallHeightPx);
      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      //Debug.WriteLine($"Create small bm: {sw.Elapsed.TotalMilliseconds} ms"); sw.Restart();


      //smallGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
      smallGraphics.InterpolationMode = InterpolationMode.Bilinear;
      //smallGraphics.InterpolationMode = InterpolationMode.Low;
      Rectangle srcRect = new(0, 0, (int)Math.Ceiling(aspectRatio * _largeBitmap.Height), _largeBitmap.Height);
      Rectangle dstRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);

      //smallGraphics.Clear(Color.Pink); // so we can detect problems - all pink should get covered up
      smallGraphics.DrawImage(_largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);
      //Debug.WriteLine($"Scale: {sw.Elapsed.TotalMilliseconds} ms"); sw.Restart();
      //Debug.WriteLine($"Total: {sw2.Elapsed.TotalMilliseconds} ms\n");

      return smallBitmap;
   }

   public Bitmap xCreateGlyph(uint charHeightPx, char c)
   {
      double aspectRatio = _charMetrics[c].CellWidth / _allCharsObservedMetrics.CharHeight;

      //
      // Step 1 - draw the character to a bitmap of height 2048
      //

      // create a font such that all characters fit the height of the large bitmap
      int largeHeightPx = 2048;
      int largeWidthPx = (int)Math.Ceiling(aspectRatio * largeHeightPx);
      using Bitmap largeBitmap = new Bitmap(largeWidthPx, largeHeightPx);
      using Graphics largeGraphics = Graphics.FromImage(largeBitmap);

      double largeFontSizePx = _allCharsObservedMetrics.RealPxToFontSizePx(largeHeightPx);

      Font font = new Font(
         FontFamily,
         (float)largeFontSizePx,
         FontStyle,
         GraphicsUnit.Pixel);

      ObservedMetrics largeCharMetrics = _allCharsObservedMetrics.WithCharHeight(largeHeightPx);
      double largeTopMarginPx = largeCharMetrics.TopMargin;

      // draw the character
      Point pt = new(0, (int)-largeTopMarginPx); // offset the top
      largeGraphics.DrawPreciseString(c, font, pt, Color.White, Color.Transparent);

      //
      // Scale the larger bitmap to the smaller one
      //
      int smallHeightPx = (int)charHeightPx;
      int smallWidthPx = (int)Math.Ceiling(aspectRatio * smallHeightPx);
      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      /*
      Debug.WriteLine($"'{c}' {c.ToHex()}");
      Debug.WriteLine($"AR: {aspectRatio}");
      Debug.WriteLine($"Large Bitmap: {largeBitmap.Width}x{largeBitmap.Height} AR:{(float)largeBitmap.Width / largeBitmap.Height}");
      Debug.WriteLine($"Small Bitmap: {smallBitmap.Width}x{smallBitmap.Height} AR:{(float)smallBitmap.Width / smallBitmap.Height}");
      */

      //smallGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
      smallGraphics.InterpolationMode = InterpolationMode.Bilinear;
      //smallGraphics.InterpolationMode = InterpolationMode.Low;
      Rectangle srcRect = new(0, 0, largeBitmap.Width, largeBitmap.Height);
      Rectangle dstRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);

      //smallGraphics.Clear(Color.Pink); // so we can detect problems - all pink should get covered up
      smallGraphics.DrawImage(largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);

      return smallBitmap;
   }

   public VLWFont CreateFont(uint charHeightPx)
   {
      VLWFont vlw = new();
      vlw.Ascent = (uint)charHeightPx / 2;
      vlw.Descent = (int)(charHeightPx - vlw.Ascent);
      vlw.FontSizePx = charHeightPx;

      for (char c = (char)0x21; c < 0x7F; c++)
      {
         ObservedMetrics charMetrics = _charMetrics[c].WithCharHeight(charHeightPx);
         //ObservedMetrics charMetrics = _allCharsObservedMetrics.WithCharHeight(targetCharHeightPx);

         VLWGlyph glyph = new();
         glyph.uChar = c;
         glyph.Bitmap = CreateGlyph(charHeightPx, c);

         glyph.gdX = 0;
         glyph.gdY = 0;
         glyph.Width = glyph.Bitmap.Width;
         glyph.Height = glyph.Bitmap.Height;
         glyph.gxAdvance = glyph.Bitmap.Height;

         vlw.Glyphs.Add(c, glyph);

         /*
         glyph.dX = (int) Math.Ceiling(charMetrics.LeftMargin);
         glyph.dY = (int) Math.Ceiling(charMetrics.TopMargin);
         glyph.Width = (int)Math.Ceiling(charMetrics.CharWidth);
         glyph.Height = (int) Math.Ceiling(charMetrics.CharHeight);
         glyph.gxAdvance = (int) Math.Ceiling(charMetrics.CharWidth);
         */
      }

      return vlw;
   }


   public static FontBuilder ForFontFamily(string fontFamily, FontStyle fontStyle)
   {
      FontBuilder builder = new FontBuilder();
      builder.SetFontFamily(fontFamily, fontStyle);
      return builder;
   }
}
