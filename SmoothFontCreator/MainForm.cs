using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public partial class MainForm : Form
{
   Color GlyphBgColorDark = Color.FromArgb(255, 60, 0, 0);
   Color GlyphBgColorLight = Color.FromArgb(255, 80, 0, 0);

   private ObservedMetrics _allCharsObservedMetrics;
   private Dictionary<char, ObservedMetrics> _charMetrics = [];
   private Bitmap _largeBitmap;

   public MainForm()
   {
      InitializeComponent();

      FontComboBox.Items.Add("Arial");
      FontComboBox.Items.Add("Times New Roman");
      FontComboBox.Items.Add("Roboto");
      FontComboBox.Items.Add("Roboto Mono");
      FontComboBox.SelectedIndex = 0;
      FontComboBox.SelectedValueChanged += FontComboBox_SelectedValueChanged;


      CharPanel.Paint += CharPanel_Paint;
      AllCharsPanel.Paint += AllCharsPanel_Paint;
      CharPreviewPanel.Paint += CharPreviewPanel_Paint;

      CharPanel.MouseWheel += CharPanel_MouseWheel;
      CharPreviewPanel.MouseWheel += CharPanel_MouseWheel;




      _UpdateMetrics();
   }

   private void _UpdateMetrics()
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Stopwatch sw = Stopwatch.StartNew();

      using Bitmap bitmap = new(CharPanel.Width, CharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, bitmapGraphics, 0.8f * CharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         FontStyle.Regular,
         GraphicsUnit.Pixel);
      _allCharsObservedMetrics = new(font);

      // first draw the designated character and measure it
      string testChar = TestCharTextBox.Text.Substring(0, 1);
      float heightPx = font.GetHeight();
      float widthPx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;

      Point pt = new((int)(CharPanel.Width - widthPx) / 2, (int)(CharPanel.Height - heightPx) / 2);
      bitmapGraphics.DrawPreciseString(testChar, font, pt, Color.White, Color.Black);

      // then draw all the charcters and measure again
      for (char c = (char)0x20; c < (char)0x7F; c++)
      {
         bitmapGraphics.Clear(Color.Transparent);

         widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
         pt = new((int)(CharPanel.Width - widthPx) / 2, (int)(CharPanel.Height - heightPx) / 2);
         bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
         ObservedMetrics charMetrics = new(bitmap, font);
         _charMetrics[c] = charMetrics;

         //Debug.WriteLine($"{((int) c).ToString("X")} '{c}'");

         _allCharsObservedMetrics.Expand(charMetrics);
      }

      Debug.WriteLine($"Font Metrics Collected in {sw.ElapsedMilliseconds}ms");
      Debug.WriteLine($"   Requested fontSizePx: {fontSizePx}");
      Debug.WriteLine($"   font.Size: {font.Size}");
      Debug.WriteLine($"   font.SizeInPoints: {font.SizeInPoints}");
      Debug.WriteLine($"   font.Height: {font.Height}");
      Debug.WriteLine($"   font.GetHeight: {font.GetHeight()}");
      Debug.WriteLine($"   Observed Metrics for all chars:");
      Debug.WriteLine($"      CharRect: {_allCharsObservedMetrics.CharRectString}");
      Debug.WriteLine($"      CellRect: {_allCharsObservedMetrics.CellRectString}");
   }

   private void CharPanel_MouseWheel(object sender, MouseEventArgs e)
   {
      char c = TestCharTextBox.Text.Length > 0 ? TestCharTextBox.Text[0] : 'A';
      if (e.Delta > 0)
      {
         c--;
      }
      else if (e.Delta < 0)
      {
         c++;
      }
      if (c < 0x20)
      {
         c = (char)0x7E;
      }
      else if (c > 0x7E)
      {
         c = (char)0x20;
      }

      TestCharTextBox.Text = c.ToString();

      ObservedMetrics m = _charMetrics[c];
      Debug.WriteLine($"'{c}' {((int)c).ToString("X")}");
      Debug.WriteLine($"   CharRect: {m.CharRectString}");
      Debug.WriteLine($"   CellRect: {m.CellRectString}");

      CharPanel.Invalidate();
      CharPreviewPanel.Invalidate();
   }

   private void FontComboBox_SelectedValueChanged(object sender, EventArgs e)
   {
      _UpdateMetrics();

      float oldSize = TestCharsTextBox.Font.Size;
      TestCharsTextBox.Font = new Font(FontComboBox.Text, oldSize);
      CharPanel.Invalidate();
      CharPreviewPanel.Invalidate();
      AllCharsPanel.Invalidate();
   }

   private void AllCharsPanel_Paint(object sender, PaintEventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Font font = new Font(
         fontFamily,
         32,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      string chars = TestCharsTextBox.Text;

      e.Graphics.DrawPreciseString(chars, font, new Point(0, 0), Color.White, Color.Transparent);
   }

   private void CharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0 || TestCharTextBox.Text[0] == ' ')
      {
         return;
      }

      Stopwatch sw = Stopwatch.StartNew();

      FontFamily fontFamily = new(FontComboBox.Text);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, e.Graphics, 0.8f * CharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      string testChar = TestCharTextBox.Text.Substring(0, 1);
      float heightPx = font.GetHeight(e.Graphics);
      float widthPx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;

      Point pt = new((int)(CharPanel.Width - widthPx) / 2, (int)(CharPanel.Height - heightPx) / 2);
      e.Graphics.DrawPreciseString(testChar, font, pt, Color.White, Color.Black);

      // draw a circle at the source point for the character
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      float radius = 20;
      e.Graphics.DrawEllipse(new Pen(Color.HotPink), pt.X - radius / 2, pt.Y - radius / 2, radius, radius);
      radius = 3;
      e.Graphics.FillEllipse(new SolidBrush(Color.HotPink), pt.X - radius / 2, pt.Y - radius / 2, radius, radius);

      // draw GDI metrics on the left
      GdiMetrics gdiMetrics = font.GetGdiMetrics();

      Color startEndColor = Color.White;
      Color topColor = Color.Pink;
      Color baselineColor = Color.LightGreen;
      Color bottomColor = Color.LightCyan;
      Color allCharsColor = Color.Yellow;
      Color oneCharColor = Color.LightCoral;

      using Pen startEndPen = new Pen(startEndColor);
      using Pen topPen = new(topColor);
      using Pen baselinePen = new(baselineColor);
      using Pen bottomPen = new(bottomColor);
      using Pen allCharsPen = new(allCharsColor);
      using Pen oneCharPen = new(oneCharColor);

      startEndPen.DashStyle = DashStyle.Dot;
      topPen.DashStyle = DashStyle.Dot;
      baselinePen.DashStyle = DashStyle.Dot;
      bottomPen.DashStyle = DashStyle.Dot;
      allCharsPen.DashStyle = DashStyle.Dot;
      oneCharPen.DashStyle = DashStyle.Dot;

      float x1 = 0;
      float x2 = CharPanel.Width / 2;

      e.Graphics.SmoothingMode = SmoothingMode.None; // so that dashes are displayed
      float start = pt.Y;
      float top = pt.Y + gdiMetrics.TopPx;
      float baseline = pt.Y + gdiMetrics.BaselinePx;
      float bottom = pt.Y + gdiMetrics.BottomPx;
      float end = pt.Y + gdiMetrics.LineSpacingPx;
      e.Graphics.DrawLine(startEndPen, x1, start, x2, start);
      e.Graphics.DrawLine(topPen, x1, top, x2, top);
      e.Graphics.DrawLine(baselinePen, x1, baseline, x2, baseline);
      e.Graphics.DrawLine(bottomPen, x1, bottom, x2, bottom);
      e.Graphics.DrawLine(startEndPen, x1, end, x2, end);

      using Font f = new Font("Arial", 10);
      e.Graphics.DrawString("START", f, new SolidBrush(startEndColor), new PointF(0, start));
      e.Graphics.DrawString("TOP", f, new SolidBrush(topColor), new PointF(0, top));
      e.Graphics.DrawString("BASELINE", f, new SolidBrush(baselineColor), new PointF(0, baseline));
      e.Graphics.DrawString("BOTTOM", f, new SolidBrush(bottomColor), new PointF(0, bottom));
      e.Graphics.DrawString("END (NEXT LINE)", f, new SolidBrush(startEndColor), new PointF(0, end));

      // draw what we observed on the right
      x1 = CharPanel.Width / 2;
      x2 = CharPanel.Width;

      float allCellTop = (float)_allCharsObservedMetrics.CellTop;
      float allCharTop = (float)_allCharsObservedMetrics.CharTop;
      float allCharBottom = (float)_allCharsObservedMetrics.CharBottom;
      float allCellBottom = (float)_allCharsObservedMetrics.CellBottom;

      e.Graphics.DrawLine(startEndPen, x1, allCellTop, x2, allCellTop);
      e.Graphics.DrawLine(startEndPen, x1, allCellBottom, x2, allCellBottom);
      e.Graphics.DrawLine(allCharsPen, x1, allCharTop, x2, allCharTop);
      e.Graphics.DrawLine(allCharsPen, x1, allCharBottom, x2, allCharBottom);

      StringFormat format = new() { Alignment = StringAlignment.Far };
      int lastX = CharPanel.Width - 1;
      e.Graphics.DrawString("All CELLS", f, new SolidBrush(startEndColor), new PointF(lastX, allCellTop), format);
      e.Graphics.DrawString("ALL CHARS", f, new SolidBrush(allCharsColor), new PointF(lastX, allCharTop), format);

      // draw lines for just this char
      x2 -= 0.2f * CharPanel.Width;
      ObservedMetrics oneCharObservedMetrics = _charMetrics[TestCharTextBox.Text[0]];
      float oneCharTop = (float)oneCharObservedMetrics.CharTop;
      float oneCharBottom = (float)oneCharObservedMetrics.CharBottom;
      e.Graphics.DrawLine(oneCharPen, x1, oneCharTop, x2, oneCharTop);
      e.Graphics.DrawLine(oneCharPen, x1, oneCharBottom, x2, oneCharBottom);

      float y1 = 0;
      float y2 = CharPanel.Height;
      float allCharsLeft = (float)_allCharsObservedMetrics.CharLeft;
      float allCharsRight = (float)_allCharsObservedMetrics.CharRight;
      float allCellsLeft = (float)_allCharsObservedMetrics.CellLeft;
      float allCellsRight = (float)_allCharsObservedMetrics.CellRight;
      float oneCharLeft = (float)oneCharObservedMetrics.CharLeft;
      float oneCharRight = (float)oneCharObservedMetrics.CharRight;
      e.Graphics.DrawLine(startEndPen, allCellsLeft, y1, allCellsLeft, y2);
      e.Graphics.DrawLine(startEndPen, allCellsRight, y1, allCellsRight, y2);
      e.Graphics.DrawLine(allCharsPen, allCharsLeft, y1, allCharsLeft, y2);
      e.Graphics.DrawLine(allCharsPen, allCharsRight, y1, allCharsRight, y2);
      e.Graphics.DrawLine(oneCharPen, oneCharLeft, y1, oneCharLeft, y2);
      e.Graphics.DrawLine(oneCharPen, oneCharRight, y1, oneCharRight, y2);

      Debug.WriteLine($"CharPanel Paint: {sw.Elapsed.TotalMilliseconds}");
   }

   private void _fillCheckerboard(Bitmap bitmap)
   {
      for (int x = 0; x < bitmap.Width; x++)
      {
         for (int y = 0; y < bitmap.Height; y++)
         {
            Color c = bitmap.GetPixel(x, y);

            if (c.A == 0)
            {
               bitmap.SetPixel(x, y, ((x + y) % 2 == 0) ? GlyphBgColorDark : GlyphBgColorLight);
            }
         }
      }
   }

   private Bitmap _createGlyph(FontFamily fontFamily, uint charHeightPx, char c)
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
         fontFamily,
         (float)largeFontSizePx,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      ObservedMetrics largeCharMetrics = _allCharsObservedMetrics.WithCharHeight(largeHeightPx);
      double largeTopMarginPx = largeCharMetrics.TopMargin;

      // draw the character
      Point pt = new(0, (int)-largeTopMarginPx); // offset the top
      largeGraphics.DrawPreciseString(c, font, pt, Color.White, Color.Transparent);

      //
      // Scale the larger bitmap to the smaller one
      //
      int smallHeightPx = int.Parse(DesiredHeightTextBox.Text);
      int smallWidthPx = (int)Math.Ceiling(aspectRatio * smallHeightPx);
      Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      Debug.WriteLine($"'{c}' {c.ToHex()}");
      Debug.WriteLine($"AR: {aspectRatio}");
      Debug.WriteLine($"Large Bitmap: {largeBitmap.Width}x{largeBitmap.Height} AR:{(float)largeBitmap.Width / largeBitmap.Height}");
      Debug.WriteLine($"Small Bitmap: {smallBitmap.Width}x{smallBitmap.Height} AR:{(float)smallBitmap.Width / smallBitmap.Height}");

      //smallGraphics.InterpolationMode = InterpolationMode.HighQualityBicubic;
      smallGraphics.InterpolationMode = InterpolationMode.Bilinear;
      //smallGraphics.InterpolationMode = InterpolationMode.Low;
      Rectangle srcRect = new(0, 0, largeBitmap.Width, largeBitmap.Height);
      Rectangle dstRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);

      //smallGraphics.Clear(Color.Pink); // so we can detect problems - all pink should get covered up
      smallGraphics.DrawImage(largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);

      return smallBitmap;
   }

   private void CharPreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0)
      {
         return;
      }

      if (uint.TryParse(DesiredHeightTextBox.Text, out uint targetFontSizePx) == false)
      {
         return;
      }

      FontFamily fontFamily = new FontFamily(FontComboBox.Text);
      char testChar = TestCharTextBox.Text[0];
      double aspectRatio = _charMetrics[testChar].CellWidth / _allCharsObservedMetrics.CharHeight;


      Bitmap smallBitmap = _createGlyph(fontFamily, targetFontSizePx, testChar);

      // fill the background with a checkerboard pattern to make it easier to see the edges of the character
      _fillCheckerboard(smallBitmap);

      // draw the bitmap to the preview char panel
      //e.Graphics.DrawImageUnscaled(smallBitmap, 10, 10);
      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      Rectangle srcRect = new(-1, -1, smallBitmap.Width + 2, smallBitmap.Height + 2);

      float pixelSize = CharPreviewPanel.Height / (float)smallBitmap.Height;
      int x = (int)((CharPreviewPanel.Width - pixelSize * smallBitmap.Width) / 2.0);
      int y = (int)((CharPreviewPanel.Height - pixelSize * smallBitmap.Height) / 2.0);
      int w = (int)(smallBitmap.Width * pixelSize);
      int h = (int)(smallBitmap.Height * pixelSize);
      Rectangle dstRect = new(x, y, w, h);
      //dstRect = new(x, y, (int)(CharPreviewPanel.Height * ((float)smallBitmap.Width / smallBitmap.Height)), CharPreviewPanel.Height);
      e.Graphics.DrawImage(smallBitmap, dstRect, srcRect, GraphicsUnit.Pixel);
   }


   private void DesiredHeightTextBox_TextChanged(object sender, EventArgs e)
   {
      if (int.TryParse(DesiredHeightTextBox.Text, out int fontSizePx))
      {
         /*
         FontFamily fontFamily = new FontFamily(FontComboBox.Text);
         ObservedSize metrics = ObservedMetrics.ForAllChars(fontFamily);
         ComputedWidthTextBox.Text = (fontSizePx * metrics.CharRect.Width).ToString();
         */

         CharPreviewPanel.Invalidate();
      }
   }

   private void TestCharTextBox_TextChanged(object sender, EventArgs e)
   {
      CharPanel.Invalidate();
      CharPreviewPanel.Invalidate();

      if (TestCharTextBox.Text.Length != 1)
      {
         HexLabel.Text = "----";
      }
      else
      {
         HexLabel.Text = "0x" + ((int)TestCharTextBox.Text[0]).ToString("X");
      }
   }

   class CharMax
   {
      public string chars = "";
      public double value = double.NaN;

      public void checkMax(char c, double value)
      {
         if (Double.IsNaN(this.value))
         {
            this.chars = c.ToString();
            this.value = value;
         }
         else
         {
            if (value > this.value)
            {
               chars = c.ToString();
               this.value = value;
            }
            else if (value == this.value)
            {
               chars = chars + c;
            }

         }
      }

      public void checkMin(char c, double value)
      {
         if (Double.IsNaN(this.value))
         {
            this.chars = c.ToString();
            this.value = value;
         }
         else
         {
            if (value < this.value)
            {
               chars = c.ToString();
               this.value = value;
            }
            else if (value == this.value)
            {
               chars = chars + c;
            }

         }
      }
   }

   private void button2_Click(object sender, EventArgs e)
   {
      /*
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      CharMax tallestChar = new();
      CharMax widestChar = new();
      CharMax highestChar = new();
      CharMax lowestChar = new();
      CharMax leftistChar = new();
      CharMax rightestChar = new();

      ObservedSize singleMetrics = ObservedMetrics.ForChar(fontFamily, (char)0x21);
      for (char c = (char)0x21; c < 0x7e; c++)
      {
         singleMetrics = ObservedMetrics.ForChar(fontFamily, c);
         tallestChar.checkMax(c, singleMetrics.CharRect.Height);
         widestChar.checkMax(c, singleMetrics.CharRect.Width);
         highestChar.checkMin(c, singleMetrics.CharRect.Top);
         lowestChar.checkMax(c, singleMetrics.CharRect.Bottom);
         leftistChar.checkMin(c, singleMetrics.CharRect.Left);
         rightestChar.checkMax(c, singleMetrics.CharRect.Right);
      }
      Debug.WriteLine($"Tallest char: '{tallestChar.chars}' {tallestChar.value}");
      Debug.WriteLine($"Widest char: '{widestChar.chars}' {widestChar.value}");
      Debug.WriteLine($"Highest char: '{highestChar.chars}' {highestChar.value}");
      Debug.WriteLine($"Lowest char: '{lowestChar.chars}' {lowestChar.value}");
      Debug.WriteLine($"Leftist char: '{leftistChar.chars}' {leftistChar.value}");
      Debug.WriteLine($"Rightist char: '{rightestChar.chars}' {rightestChar.value}");
      */
   }

   private void panel1_Paint(object sender, PaintEventArgs e)
   {
      if (_largeBitmap != null)
      {
         e.Graphics.DrawImage(_largeBitmap, new Point(0, 0));
      }
   }

   private void SaveButton_Click(object sender, EventArgs e)
   {
      /*
      byte[] bytes = File.ReadAllBytes(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");

      // process a byte stream
      VLWContent content = new(bytes);
      */

      if (uint.TryParse(DesiredHeightTextBox.Text, out uint targetCharHeightPx) == false)
      {
         return;
      }
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      VLWFont vlw = new();
      vlw.Ascent = (int)targetCharHeightPx / 2;
      vlw.Descent = (int)(targetCharHeightPx - vlw.Ascent);
      vlw.FontSizePx = targetCharHeightPx;

      for (char c = (char)0x21; c < 0x7F; c++)
      {
         ObservedMetrics charMetrics = _charMetrics[c].WithCharHeight(targetCharHeightPx);
         //ObservedMetrics charMetrics = _allCharsObservedMetrics.WithCharHeight(targetCharHeightPx);

         VLWGlyph glyph = new();
         glyph.uChar = c;
         glyph.Bitmap = _createGlyph(fontFamily, targetCharHeightPx, c);

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

      vlw.SaveAsSmoothFont(@"C:\SourceCode\Arduino\libraries\Woyak\Fonts\Scott.h");
   }

   private void VLWPreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      VLWFont font = new(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");

      // create the bitmap that we will display
      using Bitmap bitmap = new(VLWPreviewPanel.Width, VLWPreviewPanel.Height);
      using Graphics graphics = Graphics.FromImage(bitmap);

      String str = VLWTextBox.Text;

      int x = 0;
      int y = 0;
      for (int i = 0; i < str.Length; i++)
      {
         char c = str[i];

         switch (c)
         {
            case '\n':
               y += (font.Ascent + Math.Abs(font.Descent));
               x = 0;
               break;

            case '\r':
               break;

            default:
               {
                  VLWGlyph glyph = font.Glyphs[c];
                  if (glyph.Bitmap != null) // space char
                  {
                     int gx = x + glyph.gdX;
                     int gy = y + font.Ascent - glyph.gdY;
                     if (showGlyphsCheckBox.Checked)
                     {
                        graphics.FillRectangle(new SolidBrush(GlyphBgColorLight), gx, gy, glyph.Bitmap.Width, glyph.Bitmap.Height);
                     }
                     graphics.DrawImageUnscaled(glyph.Bitmap, gx, gy);
                  }
                  x += glyph.gxAdvance;
               }
               break;
         }
      }

      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      Rectangle srcRect = new(-1, -1, bitmap.Width + 2, bitmap.Height + 2);

      int pixelSize = (int)magnificationUpDown.Value;
      Rectangle dstRect = new(0, 0, bitmap.Width * pixelSize, bitmap.Height * pixelSize);
      e.Graphics.DrawImage(bitmap, dstRect, srcRect, GraphicsUnit.Pixel);
   }

   private void VLWTextBox_TextChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
   }

   private void showGlyphsCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
   }

   private void magnificationUpDown_ValueChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
   }

   private void TestCharsTextBox_TextChanged(object sender, EventArgs e)
   {
      AllCharsPanel.Invalidate();
   }
}
