
using System.Diagnostics;
using System.Text;

namespace SmoothFontCreator;

public enum MonospaceMode
{
   None,
   Widest,
   AspectRatio,
}

public class FontBuilderOptions
{
   public MonospaceMode MonospaceMode = MonospaceMode.None;
   public uint HorizontalPadding = 0;
   public uint VerticalPadding = 0;
   public double AspectRatio = 1;
   public bool MakeDigitsMonospace = false;

   public override string ToString()
   {
      return
         $"Horizontal Padding: {HorizontalPadding}\n" +
         $"Vertical Padding: {VerticalPadding}\n" +
         $"Monospace Mode: {MonospaceMode}\n" +
         (MonospaceMode == MonospaceMode.AspectRatio ? $"Aspect Ratio: {AspectRatio}\n" : "") +
         $"Make Digits Monospace: {MakeDigitsMonospace}\n";
   }
}

public class FontBuilder
{
   const int Size = 1024;
   private Bitmap _largeBitmap = new Bitmap(Size, Size);
   private Graphics _largeGraphics;
   private ObservedMetrics _allCharsMetrics;
   public Dictionary<char, ObservedMetrics> CharMetrics = [];

   public Bitmap LargeBitmap => _largeBitmap;

   public FontFamily FontFamily { get; private set; } = new FontFamily("Arial");

   public FontStyle FontStyle { get; private set; } = FontStyle.Regular;

   private FontBuilder(string fontFamilyName, FontStyle fontStyle)
   {
      FontFamily = new FontFamily(fontFamilyName);
      FontStyle = fontStyle;

      // do a first pass analysis at an arbitraily large size
      using Bitmap bitmap = new Bitmap(Size, Size);
      using Graphics graphics = Graphics.FromImage(bitmap);

      // create a font that will fill our bitmap
      float fontSizePx = (float)Math.Ceiling(FontUtil.GetFontSizePxForCellHeightPx(FontFamily, FontStyle, bitmap.Height));
      Font font = new Font(FontFamily, fontSizePx, fontStyle, GraphicsUnit.Pixel);
      _allCharsMetrics = new(font);

      Point pt = new(0, 0);

      // draw and measure each character
      for (char c = (char)0x21; c < (char)0x7F; c++)
      {
         graphics.Clear(Color.Transparent);
         graphics.DrawPreciseString(c, font, pt, Color.White, Color.Black);

         CharMetrics[c] = new ObservedMetrics(bitmap, font); ;
         _allCharsMetrics.Expand(CharMetrics[c]);
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

      ObservedMetrics metrics = CharMetrics[c];

      // TODO optimize this so that symmetric characters like '0' are drawn equally on each side
      // draw the character against the top and left
      Point pt = new((int)-Math.Floor(metrics.LeftMargin), (int)-Math.Floor(metrics.TopMargin));
      //_largeGraphics.DrawPreciseString(c, metrics.Font, pt, Color.Yellow, Color.Green);
      _largeGraphics.DrawPreciseString(c, metrics.Font, pt, Color.White, Color.Transparent);
   }

   private void _Scale(Bitmap largeBitmap, Rectangle srcRect, Bitmap smallBitmap, double xRatio, double yRatio, double xOffset, double yOffset)
   {
      using DirectBitmap large = new(largeBitmap, true, srcRect);
      using DirectBitmap small = new(smallBitmap, false);

      for (int y = 0; y < small.Height; y++)
      {
         for (int x = 0; x < small.Width; x++)
         {
            // for each pixel in the small bitmap, sum the contents of the associated
            // pixels in the large bitmap
            RectangleF rect = new RectangleF(
               (float)(x * xRatio + xOffset),
               (float)(y * yRatio + yOffset),
               (float)xRatio,
               (float)yRatio
               );

            Color c = large.GetPixel(rect);
            small.SetPixel(x, y, c);
         }
      }
   }

   private string _digits = "0123456789.+-=/";

   private bool _IsDigit(char c)
   {
      return _digits.IndexOf(c) != -1;
   }

   private double _GetMaxDigitCellWidth()
   {
      double cellWidth = 0;
      foreach (char c in _digits)
      {
         cellWidth = Math.Max(cellWidth, CharMetrics[c].CellWidth);
      }
      return cellWidth;
   }

   private VLWGlyph _CreateGlyph(VLWFont font, char c, uint cellHeightPx, FontBuilderOptions options)
   {
      Profiler.C = c;

      ObservedMetrics smallCharsMetrics = _allCharsMetrics.WithCharHeight(cellHeightPx - options.VerticalPadding);
      double scaleFactor = smallCharsMetrics.CellHeight / _allCharsMetrics.CellHeight;
      ObservedMetrics thisCharMetrics = CharMetrics[c].WithScaleFactor(scaleFactor);

      VLWGlyph glyph = new(font);
      glyph.uChar = c;

      //
      // Step 1 - Draw the character to a large bitmap
      //
      _updateLargeBitmap(c);

      ObservedMetrics charMetrics = CharMetrics[c];

      //
      // Step 2 - Scale the larger bitmap to the smaller one
      //

      // rounding can chop off some content, but it elimates very lightly shaded edges
      int smallWidthPx = (int)Math.Round(thisCharMetrics.CharWidth);
      int smallHeightPx = (int)Math.Round(thisCharMetrics.CharHeight);

      double compression = 1;
      switch (options.MonospaceMode)
      {
         case MonospaceMode.None:
         case MonospaceMode.Widest:
            break;

         case MonospaceMode.AspectRatio:
            {
               double s = Math.Max(1, (int)Math.Round(options.AspectRatio * cellHeightPx - options.HorizontalPadding));
               if (s < smallWidthPx)
               {
                  compression = s / smallWidthPx;
                  smallWidthPx = (int)s;
               }
            }
            break;
      }

      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      double yRatio = 1 / scaleFactor;
      double xRatio = (1 / scaleFactor) / compression;

      Rectangle srcRect = new(0, 0, (int)Math.Ceiling(charMetrics.CharWidth), (int)Math.Ceiling(charMetrics.CharHeight));
      double xOffset = -(xRatio * smallBitmap.Width - srcRect.Width) / 2;
      _Scale(_largeBitmap, srcRect, smallBitmap, xRatio, yRatio, xOffset, 0);

      glyph.Bitmap = smallBitmap;

      //
      // compute the various values for the glyph
      //
      glyph.Width = glyph.Bitmap.Width;
      glyph.Height = glyph.Bitmap.Height;

      GdiMetrics gdiMetrics = new GdiMetrics(thisCharMetrics.Font);
      glyph.gdY = (int)Math.Round(gdiMetrics.BaselinePx - thisCharMetrics.CharTop - options.VerticalPadding);

      switch (options.MonospaceMode)
      {
         case MonospaceMode.None:
            double cellWidth = thisCharMetrics.CellWidth;
            if (options.MakeDigitsMonospace && _IsDigit(c))
            {
               // TODO cache the max value
               cellWidth = Math.Round(scaleFactor * _GetMaxDigitCellWidth());
            }
            glyph.gxAdvance = (int)Math.Round(cellWidth + options.HorizontalPadding);
            glyph.gdX = (int)((glyph.gxAdvance - thisCharMetrics.CharWidth) / 2.0);
            break;

         case MonospaceMode.Widest:
            glyph.gxAdvance = (int)Math.Round(smallCharsMetrics.CellWidth + options.HorizontalPadding);
            glyph.gdX = (int)((glyph.gxAdvance - thisCharMetrics.CharWidth) / 2.0);
            break;

         case MonospaceMode.AspectRatio:
            glyph.gxAdvance = (int)Math.Ceiling(options.AspectRatio * cellHeightPx);
            glyph.gdX = Math.Max(0, (int)((glyph.gxAdvance - glyph.Width) / 2.0));
            break;
      }

      return glyph;
   }

   public VLWFont CreateFont(uint charHeightPx, FontBuilderOptions options)
   {
      VLWFont vlw = new();

      ObservedMetrics smallCharsMetrics = _allCharsMetrics.WithCharHeight(charHeightPx - options.VerticalPadding);

      // use GDI to get the baseline
      GdiMetrics gdiMetrics = new GdiMetrics(smallCharsMetrics.Font);

      vlw.Ascent = (uint)Math.Floor(gdiMetrics.AscentPx - smallCharsMetrics.TopMargin + options.VerticalPadding);
      vlw.Descent = (int)(charHeightPx - vlw.Ascent);
      vlw.FontSizePx = charHeightPx;

      for (char c = (char)0x21; c < 0x7F; c++)
      {
         VLWGlyph glyph = _CreateGlyph(vlw, c, charHeightPx - options.VerticalPadding, options);

         // sometimes rounding errors lead to the image being outside the cell.
         // This check brings things back
         if (vlw.Ascent - glyph.gdY + glyph.Bitmap.Height > charHeightPx)
         {
            glyph.gdY = (int)(vlw.Ascent + glyph.Bitmap.Height - charHeightPx);
         }
         vlw.Glyphs.Add(c, glyph);
      }

      return vlw;
   }

   public void Save(string folderPath, FontBuilderOptions options)
   {
      // some usefull strings
      string fontFamilyName = FontFamily.Name.Replace(" ", String.Empty);
      string fontFamilyNameAndStyle = fontFamilyName;
      fontFamilyNameAndStyle += FontStyle.IsBold() ? "Bold" : String.Empty;
      fontFamilyNameAndStyle += FontStyle.IsItalic() ? "Italic" : String.Empty;

      StringBuilder sb = new();

      sb.Append("#pragma once\n");
      sb.Append('\n');
      sb.Append("//\n");

      sb.Append($"// VLW Font Generated by Smooth Fonts Generator on {DateTime.Now.ToString("MM/dd/yyyy")}\n");
      sb.Append("//\n");

      sb.Append($"//   Font: {fontFamilyName}\n");
      sb.Append($"//   Font Style: {FontStyle}\n");

      string[] lines = options.ToString().Split('\n', StringSplitOptions.RemoveEmptyEntries);
      foreach (string line in lines)
      {
         sb.Append("//   " + line + "\n");
      }

      sb.Append("//\n");
      sb.Append('\n');

      for (uint i = 1; i < 8; i++)
      {
         uint charHeightPx = 8 * i;
         string name = fontFamilyNameAndStyle + "_" + charHeightPx;
         VLWFont font = CreateFont(charHeightPx, options);
         sb.Append(font.ToSmoothFontCppCode(name));
      }

      sb.Append('\n');
      sb.Append('\n');
      sb.Append("// for use with TFT_eSPI::setFontSize(size) where size is between 1 and 7\n");
      sb.Append($"const uint8_t* {fontFamilyNameAndStyle}[] = {{\n");
      sb.Append($"   nullptr, // there is no font size 0\n");
      for (uint i = 1; i < 8; i++)
      {
         uint charHeightPx = 8 * i;
         string name = fontFamilyNameAndStyle + "_" + charHeightPx;
         sb.Append($"   {fontFamilyNameAndStyle}_{charHeightPx},\n");
      }
      sb.Append("};\n");

      // save the file
      string fileName = FontFamily.Name;
      string filePath = folderPath + Path.DirectorySeparatorChar + fontFamilyNameAndStyle + ".h";
      File.WriteAllText(filePath, sb.ToString());

   }



   public static FontBuilder ForFontFamily(string fontFamily, FontStyle fontStyle)
   {
      return new FontBuilder(fontFamily, fontStyle);
   }
}
