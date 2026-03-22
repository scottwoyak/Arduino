using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public partial class MainForm : Form
{
   Color GlyphBgColorDark = Color.FromArgb(255, 60, 0, 0);
   Color GlyphBgColorLight = Color.FromArgb(255, 80, 0, 0);

   private ObservedMetrics _allCharsMetrics;
   private ObservedMetrics _charMetrics;
   private Bitmap _largeBitmap;
   private VLWFont _vlwFont = new(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");
   private FontBuilder _builder;

   public MainForm()
   {
      InitializeComponent();

      FontComboBox.Items.Add("Arial");
      FontComboBox.Items.Add("Times New Roman");
      FontComboBox.Items.Add("Roboto");
      FontComboBox.Items.Add("Roboto Mono");
      FontComboBox.SelectedIndex = 0;
      FontComboBox.SelectedValueChanged += FontComboBox_SelectedValueChanged;


      TrueTypeCharPanel.Paint += TrueTypeCharPanel_Paint;
      GlyphCharPanel.Paint += GlyphPanel_Paint;

      TrueTypeCharPanel.MouseWheel += OnMouseWheel;
      GlyphCharPanel.MouseWheel += OnMouseWheel;



      _builder = FontBuilder.ForFontFamily(FontComboBox.Text, italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular);

      _UpdatePreviewTextboxFont();
      _UpdateAllCharsMetrics();
      _UpdateCharMetrics();
      _createFont();

   }

   private void _UpdateAllCharsMetrics()
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Stopwatch sw = Stopwatch.StartNew();

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular,
         GraphicsUnit.Pixel);

      float heightPx = font.GetHeight();

      // draw all the cells
      for (char c = (char)0x20; c < (char)0x7F; c++)
      {
         float widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
         Point pt = new((int)(bitmap.Width - widthPx) / 2, (int)(bitmap.Height - heightPx) / 2);
         bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
      }

      // draw all the chars
      for (char c = (char)0x20; c < (char)0x7F; c++)
      {
         float widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
         Point pt = new((int)(bitmap.Width - widthPx) / 2, (int)(bitmap.Height - heightPx) / 2);
         bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Transparent);
      }

      _allCharsMetrics = new(bitmap, font);
   }

   private void _UpdateCharMetrics()
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Stopwatch sw = Stopwatch.StartNew();

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular,
         GraphicsUnit.Pixel);

      float heightPx = font.GetHeight();

      // draw the designated character
      char c = TestCharTextBox.Text[0];
      float widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
      Point pt = new((int)(bitmap.Width - widthPx) / 2, (int)(bitmap.Height - heightPx) / 2);
      bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
      _charMetrics = new(bitmap, font);
   }

   private void OnMouseWheel(object sender, MouseEventArgs e)
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

      _UpdateCharMetrics();
      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();
   }

   private void _UpdatePreviewTextboxFont()
   {
      float oldSize = PreviewTextBox.Font.Size;
      PreviewTextBox.Font = new Font(FontComboBox.Text, oldSize, italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular);
   }

   private void _resetAll()
   {
      _UpdateAllCharsMetrics();
      _UpdateCharMetrics();
      _UpdatePreviewTextboxFont();
      _createFont();
      _builder = FontBuilder.ForFontFamily(FontComboBox.Text, italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular);

      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();
   }

   private void FontComboBox_SelectedValueChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void italicCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      _resetAll();
   }


   private void TrueTypeCharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0 || TestCharTextBox.Text[0] == ' ')
      {
         return;
      }

      FontFamily fontFamily = new(FontComboBox.Text);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, e.Graphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular,
         GraphicsUnit.Pixel);

      string testChar = TestCharTextBox.Text.Substring(0, 1);
      float heightPx = font.GetHeight(e.Graphics);
      float widthPx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;

      Point pt = new((int)(TrueTypeCharPanel.Width - widthPx) / 2, (int)(TrueTypeCharPanel.Height - heightPx) / 2);
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
      Color thisCharColor = Color.Red;

      using Pen startEndPen = new Pen(startEndColor);
      using Pen topPen = new(topColor);
      using Pen baselinePen = new(baselineColor);
      using Pen bottomPen = new(bottomColor);
      using Pen allCharsPen = new(allCharsColor);
      using Pen oneCharPen = new(thisCharColor);

      startEndPen.DashStyle = DashStyle.Dot;
      topPen.DashStyle = DashStyle.Dot;
      baselinePen.DashStyle = DashStyle.Dot;
      bottomPen.DashStyle = DashStyle.Dot;
      allCharsPen.DashStyle = DashStyle.Dot;
      oneCharPen.DashStyle = DashStyle.Dot;

      float x1 = 0;
      float x2 = TrueTypeCharPanel.Width / 2;

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

      using Font f = new Font("Arial", 8);
      e.Graphics.DrawString("START", f, new SolidBrush(startEndColor), new PointF(0, start));
      e.Graphics.DrawString("TOP", f, new SolidBrush(topColor), new PointF(0, top));
      e.Graphics.DrawString("BASELINE", f, new SolidBrush(baselineColor), new PointF(0, baseline));
      e.Graphics.DrawString("BOTTOM", f, new SolidBrush(bottomColor), new PointF(0, bottom));
      e.Graphics.DrawString("END (NEXT LINE)", f, new SolidBrush(startEndColor), new PointF(0, end));

      // draw what we observed on the right
      x1 = TrueTypeCharPanel.Width / 2;
      x2 = TrueTypeCharPanel.Width;

      float allCellTop = (float)_allCharsMetrics.CellTop;
      float allCharTop = (float)_allCharsMetrics.CharTop;
      float allCharBottom = (float)_allCharsMetrics.CharBottom;
      float allCellBottom = (float)_allCharsMetrics.CellBottom;

      e.Graphics.DrawLine(startEndPen, x1, allCellTop, x2, allCellTop);
      e.Graphics.DrawLine(startEndPen, x1, allCellBottom, x2, allCellBottom);
      e.Graphics.DrawLine(allCharsPen, x1, allCharTop, x2, allCharTop);
      e.Graphics.DrawLine(allCharsPen, x1, allCharBottom, x2, allCharBottom);

      StringFormat format = new() { Alignment = StringAlignment.Far };
      int lastX = TrueTypeCharPanel.Width - 1;
      e.Graphics.DrawString("All CELLS", f, new SolidBrush(startEndColor), new PointF(lastX, allCellTop), format);
      e.Graphics.DrawString("ALL CHARS", f, new SolidBrush(allCharsColor), new PointF(lastX, allCharTop), format);

      // draw lines for just this char
      x2 -= 0.2f * TrueTypeCharPanel.Width;
      int thisCharTop = (int)_charMetrics.CharTop;
      int thisCharBottom = (int)_charMetrics.CharBottom;
      e.Graphics.DrawLine(oneCharPen, x1, thisCharTop, x2, thisCharTop);
      e.Graphics.DrawLine(oneCharPen, x1, thisCharBottom, x2, thisCharBottom);

      int y1 = 0;
      int y2 = TrueTypeCharPanel.Height;
      int allCharsLeft = (int)_allCharsMetrics.CharLeft;
      int allCharsRight = (int)_allCharsMetrics.CharRight;
      int allCellsLeft = (int)_allCharsMetrics.CellLeft;
      int allCellsRight = (int)_allCharsMetrics.CellRight;
      int thisCharLeft = (int)_charMetrics.CharLeft;
      int thisCharRight = (int)_charMetrics.CharRight;
      e.Graphics.DrawLine(startEndPen, allCellsLeft, y1, allCellsLeft, y2);
      e.Graphics.DrawLine(startEndPen, allCellsRight, y1, allCellsRight, y2);
      e.Graphics.DrawLine(allCharsPen, allCharsLeft, y1, allCharsLeft, y2);
      e.Graphics.DrawLine(allCharsPen, allCharsRight, y1, allCharsRight, y2);
      e.Graphics.DrawLine(oneCharPen, thisCharLeft, y1, thisCharLeft, y2);
      e.Graphics.DrawLine(oneCharPen, thisCharRight, y1, thisCharRight, y2);
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

   private void _createFont()
   {
      uint targetCharHeightPx = (uint)charHeightUpDown.Value;

      _vlwFont = _builder.CreateFont(targetCharHeightPx);

      PreviewPanel.Invalidate();
   }

   private void GlyphPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0)
      {
         return;
      }

      uint targetCharHeightPx = (uint)charHeightUpDown.Value;

      char testChar = TestCharTextBox.Text[0];
      double aspectRatio = _charMetrics.CellWidth / _allCharsMetrics.CharHeight;


      Bitmap smallBitmap = _builder.CreateGlyph(targetCharHeightPx, testChar);

      // fill the background with a checkerboard pattern to make it easier to see the edges of the character
      _fillCheckerboard(smallBitmap);

      // draw the bitmap to the preview char panel
      //e.Graphics.DrawImageUnscaled(smallBitmap, 10, 10);
      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      Rectangle srcRect = new(-1, -1, smallBitmap.Width + 2, smallBitmap.Height + 2);

      float pixelSize = GlyphCharPanel.Height / (float)smallBitmap.Height;
      int x = (int)((GlyphCharPanel.Width - pixelSize * smallBitmap.Width) / 2.0);
      int y = (int)((GlyphCharPanel.Height - pixelSize * smallBitmap.Height) / 2.0);
      int w = (int)(smallBitmap.Width * pixelSize);
      int h = (int)(smallBitmap.Height * pixelSize);
      Rectangle dstRect = new(x, y, w, h);
      //dstRect = new(x, y, (int)(CharPreviewPanel.Height * ((float)smallBitmap.Width / smallBitmap.Height)), CharPreviewPanel.Height);
      e.Graphics.DrawImage(smallBitmap, dstRect, srcRect, GraphicsUnit.Pixel);
   }

   private void TestCharTextBox_TextChanged(object sender, EventArgs e)
   {
      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();

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

      string[] fontSizes = fontSizesTextBox.Text.Split(new char[] { ' ', ',' }, StringSplitOptions.RemoveEmptyEntries);

      foreach (string fontSize in fontSizes)
      {
         if (uint.TryParse(fontSize, out uint size))
         {
            VLWFont font = _builder.CreateFont(size);
            font.SaveAsSmoothFont(@"C:\SourceCode\Arduino\libraries\Woyak", "Scott" + size);
            _vlwFont.SaveAsSmoothFont(@"C:\SourceCode\Arduino\libraries\Woyak", "Scott");
         }
      }

      MessageBox.Show("Files Created");
   }

   private void PreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      // create the bitmap that we will display
      using Bitmap bitmap = new(PreviewPanel.Width, PreviewPanel.Height);
      using Graphics graphics = Graphics.FromImage(bitmap);

      String str = PreviewTextBox.Text;

      int x = 0;
      int y = 0;
      for (int i = 0; i < str.Length; i++)
      {
         char c = str[i];

         switch (c)
         {
            case '\n':
               y += (int)_vlwFont.Height;
               x = 0;
               break;

            case '\r':
               break;

            case ' ':
               {
                  if (_vlwFont.Glyphs.TryGetValue(' ', out VLWGlyph? glyph))
                  {
                     x += glyph.gxAdvance;
                  }
                  else
                  {
                     x += (int)_vlwFont.Height;
                  }
               }
               break;
            default:
               {
                  VLWGlyph glyph = _vlwFont.Glyphs[c];
                  if (glyph.Bitmap != null) // space char
                  {
                     int gx = x + glyph.gdX;
                     int gy = (int)(y + _vlwFont.Ascent - glyph.gdY);
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

   private void PreviewTextBox_TextChanged(object sender, EventArgs e)
   {
      PreviewPanel.Invalidate();
   }

   private void showGlyphsCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      PreviewPanel.Invalidate();
   }

   private void magnificationUpDown_ValueChanged(object sender, EventArgs e)
   {
      PreviewPanel.Invalidate();
   }

   private void charHeightUpDown_ValueChanged(object sender, EventArgs e)
   {
      GlyphCharPanel.Invalidate();
      _createFont();
   }

   private void VLFCharPanel_Paint(object sender, PaintEventArgs e)
   {

   }

   private void testObserveButton_Click(object sender, EventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular,
         GraphicsUnit.Pixel);

      float heightPx = font.GetHeight();

      // draw the designated character
      char c = TestCharTextBox.Text[0];
      float widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
      Point pt = new((int)(TrueTypeCharPanel.Width - widthPx) / 2, (int)(TrueTypeCharPanel.Height - heightPx) / 2);
      bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);

      Stopwatch sw = Stopwatch.StartNew();
      ObservedMetrics? om = null;
      const int numTrials = 500;
      for (int i = 0; i < numTrials; i++)
      {
         om = new(bitmap, font);
      }
      double ms = sw.Elapsed.TotalMilliseconds;

      statusTextBox.Clear();
      statusTextBox.Text += $"Profile for '{c}'\r\n";
      statusTextBox.Text += $"  Bitmap\t{bitmap.Width} {bitmap.Height}\r\n";
      statusTextBox.Text += $"  Cell:\t{om.CellWidth} {om.CellHeight}\r\n";
      statusTextBox.Text += $"\t{om.CellLeft} {om.CellRight} {om.CellTop} {om.CellBottom}\r\n";
      statusTextBox.Text += $"  Char:\t{om.CharWidth} {om.CharHeight}\r\n";
      statusTextBox.Text += $"\t{om.CharLeft} {om.CharRight} {om.CharTop} {om.CharBottom}\r\n";
      statusTextBox.Text += $"{(ms / numTrials).ToString("0.000")} ms\r\n";
   }


   private void testGlyphButton_Click(object sender, EventArgs e)
   {
      Stopwatch sw = Stopwatch.StartNew();
      uint targetCharHeightPx = (uint)charHeightUpDown.Value;
      char c = TestCharTextBox.Text[0];

      const int numTrials = 20;
      for (int i = 0; i < numTrials; i++)
      {
         _builder.CreateGlyph(targetCharHeightPx, c);
      }
      double ms = sw.Elapsed.TotalMilliseconds;

      statusTextBox.Clear();
      statusTextBox.Text += $"Profile for char height {targetCharHeightPx}\r\n";
      statusTextBox.Text += $"{(ms / numTrials).ToString("0.0")} ms\r\n";
   }

   private void testFontButton_Click(object sender, EventArgs e)
   {
      Stopwatch sw = Stopwatch.StartNew();
      const int numTrials = 1;
      uint targetCharHeightPx = (uint)charHeightUpDown.Value;
      VLWFont? font = null;
      for (int i = 0; i < numTrials; i++)
      {
         font = _builder.CreateFont(targetCharHeightPx);
      }
      double ms = sw.Elapsed.TotalMilliseconds;

      statusTextBox.Clear();
      statusTextBox.Text += $"Profile for char height {targetCharHeightPx}\r\n";
      statusTextBox.Text += $"Num Chars {font.NumChars}\r\n";
      statusTextBox.Text += $"{(ms / numTrials).ToString("0.0")} ms\r\n";
   }
}
