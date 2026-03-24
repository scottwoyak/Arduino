
namespace SmoothFontCreator;

public class FontBuilderOptions
{
   public bool Monospaced = false;
   public int Spacing = 0;
}

public class FontBuilder
{
   const int Size = 1024;
   private Bitmap _largeBitmap = new Bitmap(Size, Size);
   private Graphics _largeGraphics;
   private ObservedMetrics _allCharsMetrics;
   private Dictionary<char, ObservedMetrics> _charMetrics = [];

   public Bitmap LargeBitmap => _largeBitmap;

   public FontFamily FontFamily { get; private set; } = new FontFamily("Arial");

   public FontStyle FontStyle { get; private set; } = FontStyle.Regular;

   private FontBuilder(string fontFamilyName, FontStyle fontStyle)
   {
      FontFamily = new FontFamily(fontFamilyName);
      FontStyle = fontStyle;

      // do a first pass analysis at an arbitraily large size
      const int size = 1024;
      using Bitmap bitmap = new Bitmap(size, size);
      using Graphics graphics = Graphics.FromImage(bitmap);

      // create a font that will fill our bitmap
      float fontSizePx = (float)Math.Ceiling(FontUtil.LineSpacingPxToFontSizePx(FontFamily, FontStyle, graphics, bitmap.Height));
      Font font = new Font(FontFamily, fontSizePx, fontStyle, GraphicsUnit.Pixel);
      _allCharsMetrics = new(font);

      Point pt = new(0, 0);

      // draw and measure each character
      for (char c = (char)0x21; c < (char)0x7F; c++)
      {
         graphics.Clear(Color.Transparent);
         graphics.DrawPreciseString(c, font, pt, Color.White, Color.Black);

         _charMetrics[c] = new ObservedMetrics(bitmap, font); ;
         _allCharsMetrics.Expand(_charMetrics[c]);
      }

      // based on what we discovered, create the bitmap that we'll use to create the font
      _largeBitmap = new Bitmap((int)_allCharsMetrics.CharWidth, (int)_allCharsMetrics.CharHeight);
      _largeGraphics = Graphics.FromImage(_largeBitmap);
   }

   public void UpdateLargeBitmap(char c)
   {
      _updateLargeBitmap(c);
   }

   private void _updateLargeBitmap(char c)
   {
      _largeGraphics.Clear(Color.Transparent);

      ObservedMetrics metrics = _charMetrics[c];

      // TODO optimize this so that symmetric characters like '0' are drawn equally on each side
      // draw the character against the top and left
      Point pt = new((int)-Math.Floor(metrics.LeftMargin), (int)-Math.Floor(metrics.TopMargin));
      //_largeGraphics.DrawPreciseString(c, metrics.Font, pt, Color.Yellow, Color.Green);
      _largeGraphics.DrawPreciseString(c, metrics.Font, pt, Color.White, Color.Transparent);
   }

   private void _Scale(Bitmap largeBitmap, Rectangle srcRect, Bitmap smallBitmap, double ratio)
   {
      using DirectBitmap large = new(largeBitmap, true, srcRect);
      using DirectBitmap small = new(smallBitmap, false);

      float ratioF = (float)ratio;
      float offset = -(ratioF * smallBitmap.Width - srcRect.Width) / 2;
      for (int y = 0; y < small.Height; y++)
      {
         for (int x = 0; x < small.Width; x++)
         {
            // for each pixel in the small bitmap, sum the contents of the associated
            // pixels in the large bitmap

            if (Profiler.C == '0' && x == 0 && y == 17)
            {

            }

            RectangleF rect = new RectangleF(x * ratioF + offset, y * ratioF, ratioF, ratioF);

            Color c = large.GetPixel(rect);
            small.SetPixel(x, y, c);
         }
      }
   }

   public VLWGlyph CreateGlyph(char c, uint charHeightPx, FontBuilderOptions options)
   {
      ObservedMetrics allCharsMetrics = _allCharsMetrics.WithCharHeight(charHeightPx);
      return _CreateGlyph(c, charHeightPx, options, allCharsMetrics);
   }

   private VLWGlyph _CreateGlyph(char c, uint charHeightPx, FontBuilderOptions options, ObservedMetrics allCharsMetrics)
   {
      Profiler.C = c;

      double scaleFactor = allCharsMetrics.CellHeight / _allCharsMetrics.CellHeight;
      ObservedMetrics thisCharMetrics = _charMetrics[c].WithScaleFactor(scaleFactor);

      VLWGlyph glyph = new();
      glyph.uChar = c;

      //
      // Step 1 - Draw the character to a large bitmap
      //
      _updateLargeBitmap(c);

      ObservedMetrics charMetrics = _charMetrics[c];

      //
      // Step 2 - Scale the larger bitmap to the smaller one
      //
      int smallWidthPx = (int)Math.Ceiling(scaleFactor * charMetrics.CharWidth);
      int smallHeightPx = (int)Math.Ceiling(scaleFactor * charMetrics.CharHeight);
      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

#if USE_GDI
      double ratio = 1 / scaleFactor;
      //smallGraphics.InterpolationMode = InterpolationMode.Bilinear;
      //smallGraphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
      smallGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
      float offset = (float) (smallBitmap.Width - charMetrics.CharWidth/ratio) / 2;
      Rectangle srcRect = new(0, 0, (int) Math.Ceiling(charMetrics.CharWidth), (int) Math.Ceiling(charMetrics.CharHeight));
      RectangleF dstRect = new(offset, 0, smallBitmap.Width, smallBitmap.Height);
      //smallGraphics.DrawImage(_largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);
#else
      double ratio = 1 / scaleFactor;
      Rectangle srcRect = new(0, 0, (int)Math.Ceiling(charMetrics.CharWidth), (int)Math.Ceiling(charMetrics.CharHeight));
      _Scale(_largeBitmap, srcRect, smallBitmap, ratio);
#endif

      glyph.Bitmap = smallBitmap;

      // use GDI to get the baseline
      GdiMetrics gdiMetrics = new GdiMetrics(thisCharMetrics.Font);

      glyph.gdY = (int)Math.Ceiling(gdiMetrics.BaselinePx - thisCharMetrics.CharTop);
      glyph.Width = glyph.Bitmap.Width;
      glyph.Height = glyph.Bitmap.Height;

      if (options.Monospaced)
      {
         glyph.gxAdvance = (int)Math.Ceiling(allCharsMetrics.CellWidth) + options.Spacing;
         glyph.gdX = (int)((glyph.gxAdvance - thisCharMetrics.CellWidth) / 2.0);
      }
      else
      {
         glyph.gxAdvance = glyph.Bitmap.Width + options.Spacing;
         glyph.gdX = 0;
      }

      return glyph;
   }

   public VLWFont CreateFont(uint charHeightPx, FontBuilderOptions options)
   {
      VLWFont vlw = new();

      ObservedMetrics allCharsMetrics = _allCharsMetrics.WithCharHeight(charHeightPx);

      // use GDI to get the baseline
      GdiMetrics gdiMetrics = new GdiMetrics(allCharsMetrics.Font);

      vlw.Ascent = (uint)Math.Ceiling(gdiMetrics.AscentPx - allCharsMetrics.TopMargin);
      vlw.Descent = (int)(charHeightPx - vlw.Ascent);
      vlw.FontSizePx = charHeightPx;

      for (char c = (char)0x21; c < 0x7F; c++)
      {
         VLWGlyph glyph = _CreateGlyph(c, charHeightPx, options, allCharsMetrics);
         vlw.Glyphs.Add(c, glyph);
      }

      return vlw;
   }


   public static FontBuilder ForFontFamily(string fontFamily, FontStyle fontStyle)
   {
      return new FontBuilder(fontFamily, fontStyle);
   }
}
