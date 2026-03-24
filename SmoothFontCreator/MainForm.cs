using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public partial class MainForm : Form
{
   Color Red1 = Color.FromArgb(255, 60, 0, 0);
   Color Red2 = Color.FromArgb(255, 80, 0, 0);
   Color Red3 = Color.FromArgb(255, 110, 0, 0);

   private ObservedMetrics _allCharsMetrics;
   private VLWFont _vlwFont = new(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");
   private FontBuilder _builder;

   private FontBuilderOptions FontBuilderOptions
   {
      get
      {
         return new FontBuilderOptions()
         {
            Monospaced = monospaceCheckBox.Checked,
            Spacing = (int)SpacingUpDown.Value,
         };
      }
   }


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


      _builder = FontBuilder.ForFontFamily(FontComboBox.Text, _getFontStyle());

      _UpdatePreviewTextboxFont();
      _UpdateAllCharsMetrics();
      _createFont();
      _displayCurrentChar();
   }

   private FontStyle _getFontStyle()
   {
      FontStyle italic = italicCheckBox.Checked ? FontStyle.Italic : FontStyle.Regular;
      FontStyle bold = boldCheckBox.Checked ? FontStyle.Bold : FontStyle.Regular;
      return italic | bold;
   }

   private void _UpdateAllCharsMetrics()
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Stopwatch sw = Stopwatch.StartNew();

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, _getFontStyle(), bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
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

   private ObservedMetrics _getCharMetrics()
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, _getFontStyle(), bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
         GraphicsUnit.Pixel);

      float heightPx = font.GetHeight();

      // draw the designated character
      char c = TestCharTextBox.Text[0];
      float widthPx = TextRenderer.MeasureText(c.ToString(), font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
      Point pt = new((int)(bitmap.Width - widthPx) / 2, (int)(bitmap.Height - heightPx) / 2);
      bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);

      return new ObservedMetrics(bitmap, font);
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
   }

   private void TestCharTextBox_TextChanged(object sender, EventArgs e)
   {
      _displayCurrentChar();
   }

   private void _displayCurrentChar()
   {
      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();

      ascentTextBox.Text = _vlwFont.Ascent + " px";
      descentTextBox.Text = _vlwFont.Descent + " px";

      char c = TestCharTextBox.Text[0];
      if (TestCharTextBox.Text.Length != 1)
      {
         HexLabel.Text = "----";

         widthTextBox.Text = "--";
         heightTextBox.Text = "--";
         gxAdvanceTextBox.Text = "--";
         gdXTextBox.Text = "--";
         gdYTextBox.Text = "--";
         paddingTextBox.Text = "--";
      }
      else
      {
         HexLabel.Text = "0x" + ((int)c).ToString("X");

         if (_vlwFont.Glyphs.TryGetValue(c, out VLWGlyph? glyph))
         {
            widthTextBox.Text = glyph.Width + " px";
            heightTextBox.Text = glyph.Height + " px";
            gxAdvanceTextBox.Text = glyph.gxAdvance + " px";
            gdXTextBox.Text = glyph.gdX + " px";
            gdYTextBox.Text = glyph.gdY + " px";
            paddingTextBox.Text = glyph.padding + " px";
         }
         else
         {
            widthTextBox.Text = "--";
            heightTextBox.Text = "--";
            gxAdvanceTextBox.Text = "--";
            gdXTextBox.Text = "--";
            gdYTextBox.Text = "--";
            paddingTextBox.Text = "--";
         }
      }
   }

   private void _UpdatePreviewTextboxFont()
   {
      float oldSize = PreviewTextBox.Font.Size;
      PreviewTextBox.Font = new Font(FontComboBox.Text, oldSize, _getFontStyle());
   }

   private void _resetAll()
   {
      _builder = FontBuilder.ForFontFamily(FontComboBox.Text, _getFontStyle());
      _UpdateAllCharsMetrics();
      _UpdatePreviewTextboxFont();
      _createFont();
      _displayCurrentChar();

      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();
   }

   private void TrueTypeCharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0 || TestCharTextBox.Text[0] == ' ')
      {
         return;
      }

      FontFamily fontFamily = new(FontComboBox.Text);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, _getFontStyle(), e.Graphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
         GraphicsUnit.Pixel);

      string testChar = TestCharTextBox.Text.Substring(0, 1);
      float cellHeightPx = font.GetHeight(e.Graphics);
      float cellWidthPx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;

      Point pt = new((int)(TrueTypeCharPanel.Width - cellWidthPx) / 2, (int)(TrueTypeCharPanel.Height - cellHeightPx) / 2);
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

      ObservedMetrics charMetrics = _getCharMetrics();

      // draw lines for just this char
      x2 -= 0.2f * TrueTypeCharPanel.Width;
      int thisCharTop = (int)charMetrics.CharTop;
      int thisCharBottom = (int)charMetrics.CharBottom;
      e.Graphics.DrawLine(oneCharPen, x1, thisCharTop, x2, thisCharTop);
      e.Graphics.DrawLine(oneCharPen, x1, thisCharBottom, x2, thisCharBottom);

      int y1 = 0;
      int y2 = TrueTypeCharPanel.Height;
      int allCharsLeft = (int)_allCharsMetrics.CharLeft;
      int allCharsRight = (int)_allCharsMetrics.CharRight;
      int allCellsLeft = (int)_allCharsMetrics.CellLeft;
      int allCellsRight = (int)_allCharsMetrics.CellRight;
      int thisCharLeft = (int)charMetrics.CharLeft;
      int thisCharRight = (int)charMetrics.CharRight;
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
               bitmap.SetPixel(x, y, ((x + y) % 2 == 0) ? Red1 : Red2);
            }
         }
      }
   }

   private void _createFont()
   {
      uint targetCharHeightPx = (uint)charHeightUpDown.Value;

      _vlwFont = _builder.CreateFont(targetCharHeightPx, FontBuilderOptions);

      PreviewPanel.Invalidate();
   }

   private void GlyphPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0)
      {
         return;
      }

      char c = TestCharTextBox.Text[0];

      if (_vlwFont.Glyphs.TryGetValue(c, out VLWGlyph? glyph))
      {
         using Bitmap smallBitmap = (Bitmap)glyph.Bitmap.Clone();

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
         e.Graphics.DrawImage(smallBitmap, dstRect, srcRect, GraphicsUnit.Pixel);
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

   private void panel1_Paint(object sender, PaintEventArgs e)
   {
      _builder.UpdateLargeBitmap(TestCharTextBox.Text[0]);
      e.Graphics.DrawImageUnscaled(_builder.LargeBitmap, new Point(0, 0));
      e.Graphics.DrawRectangle(Pens.White, new Rectangle(new Point(0, 0), _builder.LargeBitmap.Size));

      //      e.Graphics.DrawImageUnscaled(_builder.xBitmap, new Point(0, 0));
      //      e.Graphics.DrawRectangle(Pens.White, new Rectangle(new Point(0,0), _builder.xBitmap.Size));
   }

   private void SaveButton_Click(object sender, EventArgs e)
   {
      string[] fontSizes = fontSizesTextBox.Text.Split(new char[] { ' ', ',' }, StringSplitOptions.RemoveEmptyEntries);

      foreach (string fontSize in fontSizes)
      {
         if (uint.TryParse(fontSize, out uint size))
         {
            VLWFont font = _builder.CreateFont(size, FontBuilderOptions);
            font.SaveAsSmoothFont(@"C:\SourceCode\Arduino\libraries\Woyak", "Scott" + size);
         }
      }

      MessageBox.Show($"{fontSizes.Length} Files Created");
   }

   private void PreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      // create the bitmap that we will display
      using Bitmap bitmap = new(PreviewPanel.Width, PreviewPanel.Height);
      using Graphics graphics = Graphics.FromImage(bitmap);

      String str = PreviewTextBox.Text;

      Brush glyphBrush = new SolidBrush(Red3);

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
                        graphics.FillRectangle(glyphBrush, gx, gy, glyph.Bitmap.Width, glyph.Bitmap.Height);
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

   private void testObserveButton_Click(object sender, EventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = (float)Math.Round(FontUtil.LineSpacingPxToFontSizePx(fontFamily, _getFontStyle(), bitmapGraphics, 0.8f * TrueTypeCharPanel.Height));

      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
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
         _builder.CreateGlyph(c, targetCharHeightPx, FontBuilderOptions);
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
         font = _builder.CreateFont(targetCharHeightPx, FontBuilderOptions);
      }
      double ms = sw.Elapsed.TotalMilliseconds;

      statusTextBox.Clear();
      statusTextBox.Text += $"Profile for char height {targetCharHeightPx}\r\n";
      statusTextBox.Text += $"Num Chars {font.NumChars}\r\n";
      statusTextBox.Text += $"{(ms / numTrials).ToString("0.0")} ms\r\n";
   }

   private void FontComboBox_SelectedValueChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void boldCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void italicCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void digitCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void monospaceCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void SpacingUpDown_ValueChanged(object sender, EventArgs e)
   {
      _resetAll();
   }

   private void button1_Click(object sender, EventArgs e)
   {
      byte[] bytes = File.ReadAllBytes(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");
      _vlwFont = new(bytes);

      _displayCurrentChar();
      PreviewPanel.Invalidate();
      TrueTypeCharPanel.Invalidate();
      GlyphCharPanel.Invalidate();
   }

}
