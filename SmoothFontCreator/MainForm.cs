using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Windows.Forms.VisualStyles;

namespace SmoothFontCreator;

public partial class MainForm : Form
{
   Color Red1 = Color.FromArgb(255, 60, 0, 0);
   Color Red2 = Color.FromArgb(255, 80, 0, 0);
   Color Red3 = Color.FromArgb(255, 110, 0, 0);
   int _magnification = 2;

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
      VLWCharPanel.Paint += GlyphPanel_Paint;

      TrueTypeCharPanel.MouseWheel += OnMouseWheel;
      VLWCharPanel.MouseWheel += OnMouseWheel;
      VLWPreviewPanel.MouseWheel += PreviewPanel_MouseWheel;
      TrueTypePreviewPanel.MouseWheel += PreviewPanel_MouseWheel;


      _builder = FontBuilder.ForFontFamily(FontComboBox.Text, _getFontStyle());

      _UpdatePreviewTextboxFont();
      _UpdateAllCharsMetrics();
      _createFont();
      _displayCurrentChar();
   }

   private void PreviewPanel_MouseWheel(object? sender, MouseEventArgs e)
   {
      if (e.Delta > 0)
      {
         if (_magnification < 100)
         {
            _magnification++;
         }
      }
      else if (e.Delta < 0)
      {
         if (_magnification > 1)
         {
            _magnification--;
         }
      }

      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
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

      float fontSizePx = FontUtil.GetFontSizePxForCellHeightPx(fontFamily, _getFontStyle(), 0.8f * TrueTypeCharPanel.Height);

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

      float fontSizePx = FontUtil.GetFontSizePxForCellHeightPx(fontFamily, _getFontStyle(), 0.8f * TrueTypeCharPanel.Height);

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
      VLWCharPanel.Invalidate();

      vlwAscentTextBox.Text = _vlwFont.Ascent + " px";
      vlwDescentTextBox.Text = _vlwFont.Descent + " px";

      GdiMetrics gdiMetrics = new(_allCharsMetrics.Font);
      ttHeightTextBox.Text = (gdiMetrics.HeightPx / gdiMetrics.HeightPx).ToString("0.000");
      ttSizeTextBox.Text = (gdiMetrics.SizePx / gdiMetrics.HeightPx).ToString("0.000");
      ttAscentTextBox.Text = (gdiMetrics.AscentPx / gdiMetrics.HeightPx).ToString("0.000");
      ttDescentTextBox.Text = (gdiMetrics.DescentPx / gdiMetrics.HeightPx).ToString("0.000");
      ttLineSpacingTextBox.Text = (gdiMetrics.LineSpacingPx / gdiMetrics.HeightPx).ToString("0.000");

      char c = TestCharTextBox.Text[0];
      if (TestCharTextBox.Text.Length != 1)
      {
         HexLabel.Text = "----";

         vlwWidthTextBox.Text = "--";
         vlwHeightTextBox.Text = "--";
         gxAdvanceTextBox.Text = "--";
         gdXTextBox.Text = "--";
         gdYTextBox.Text = "--";
         paddingTextBox.Text = "--";

         ttCharHeightTextBox.Text = "--";
         ttCellWidthTextBox.Text = "--";
         ttCellHeightTextBox.Text = "--";
         ttCharWidthTextBox.Text = "--";
      }
      else
      {
         HexLabel.Text = "0x" + ((int)c).ToString("X");

         if (_vlwFont.Glyphs.TryGetValue(c, out VLWGlyph? glyph))
         {
            vlwWidthTextBox.Text = glyph.Width + " px";
            vlwHeightTextBox.Text = glyph.Height + " px";
            gxAdvanceTextBox.Text = glyph.gxAdvance + " px";
            gdXTextBox.Text = glyph.gdX + " px";
            gdYTextBox.Text = glyph.gdY + " px";
            paddingTextBox.Text = glyph.padding + " px";

            ObservedMetrics charMetrics = _getCharMetrics();
            ttCellWidthTextBox.Text = (charMetrics.CellWidth / gdiMetrics.HeightPx).ToString("0.000");
            ttCellHeightTextBox.Text = (charMetrics.CellHeight / gdiMetrics.HeightPx).ToString("0.000");
            ttCharWidthTextBox.Text = (charMetrics.CharWidth / gdiMetrics.HeightPx).ToString("0.000");
            ttCharHeightTextBox.Text = (charMetrics.CharHeight / gdiMetrics.HeightPx).ToString("0.000");
         }
         else
         {
            vlwWidthTextBox.Text = "--";
            vlwHeightTextBox.Text = "--";
            gxAdvanceTextBox.Text = "--";
            gdXTextBox.Text = "--";
            gdYTextBox.Text = "--";
            paddingTextBox.Text = "--";

            ttCellWidthTextBox.Text = "--";
            ttCellHeightTextBox.Text = "--";
            ttCharWidthTextBox.Text = "--";
            ttCharHeightTextBox.Text = "--";
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
      VLWCharPanel.Invalidate();
   }

   private void TrueTypeCharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0 || TestCharTextBox.Text[0] == ' ')
      {
         return;
      }

      FontFamily fontFamily = new(FontComboBox.Text);

      float fontSizePx = FontUtil.GetFontSizePxForCellHeightPx(fontFamily, _getFontStyle(), 0.8f * TrueTypeCharPanel.Height);

      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
         GraphicsUnit.Pixel);

      string testChar = TestCharTextBox.Text.Substring(0, 1);
      float cellHeightPx = font.GetHeight(e.Graphics);
      Size cellSizePx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
      float cellWidthPx = cellSizePx.Width;

      Point pt = new((int)(TrueTypeCharPanel.Width - cellWidthPx) / 2, (int)(TrueTypeCharPanel.Height - cellHeightPx) / 2);
      e.Graphics.DrawPreciseString(testChar, font, pt, Color.White, Color.Black);

      // draw a circle at the source point for the character
      e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
      float radius = 20;
      e.Graphics.DrawEllipse(new Pen(Color.HotPink), pt.X - radius / 2, pt.Y - radius / 2, radius, radius);
      radius = 3;
      e.Graphics.FillEllipse(new SolidBrush(Color.HotPink), pt.X - radius / 2, pt.Y - radius / 2, radius, radius);

      //
      // draw GDI metrics on the left
      //
      GdiMetrics gdiMetrics = font.GetGdiMetrics();

      Color cellsColor = Color.White;
      Color ascentColor = Color.Pink;
      Color baselineColor = Color.LightGreen;
      Color descentColor = Color.Cyan;
      Color heightColor = Color.Violet;
      Color allCharsColor = Color.Yellow;
      Color thisCharColor = Color.Orange;

      using Pen startEndPen = new Pen(cellsColor);
      using Pen ascentPen = new(ascentColor);
      using Pen baselinePen = new(baselineColor);
      using Pen descentPen = new(descentColor);
      using Pen heightPen = new(heightColor);
      using Pen allCharsPen = new(allCharsColor);
      using Pen thisCharPen = new(thisCharColor);

      startEndPen.DashStyle = DashStyle.Dot;
      ascentPen.DashStyle = DashStyle.Dot;
      baselinePen.DashStyle = DashStyle.Dot;
      descentPen.DashStyle = DashStyle.Dot;
      allCharsPen.DashStyle = DashStyle.Dot;
      thisCharPen.DashStyle = DashStyle.Dot;

      float x1 = 0;
      float x2 = TrueTypeCharPanel.Width / 2;

      e.Graphics.SmoothingMode = SmoothingMode.None; // so that dashes are displayed
      float ascent = pt.Y + gdiMetrics.BaselinePx - gdiMetrics.AscentPx;
      float baseline = pt.Y + gdiMetrics.BaselinePx;
      float descent = pt.Y + gdiMetrics.BaselinePx + gdiMetrics.DescentPx;
      float height = pt.Y + gdiMetrics.HeightPx;
      e.Graphics.DrawLine(ascentPen, x1, ascent, x2, ascent);
      e.Graphics.DrawLine(baselinePen, x1, baseline, x2, baseline);
      e.Graphics.DrawLine(descentPen, x1, descent, x2, descent);
      e.Graphics.DrawLine(heightPen, x1, height, x2, height);

      //
      // draw what we observed on the right
      //
      x1 = TrueTypeCharPanel.Width / 2;
      x2 = TrueTypeCharPanel.Width;

      float allCellTop = (float)_allCharsMetrics.CellTop;
      float allCharsTop = (float)_allCharsMetrics.CharTop;
      float allCharsBottom = (float)_allCharsMetrics.CharBottom;
      float allCellsBottom = (float)_allCharsMetrics.CellBottom;

      e.Graphics.DrawLine(startEndPen, x1, allCellTop, x2, allCellTop);
      e.Graphics.DrawLine(startEndPen, x1, allCellsBottom, x2, allCellsBottom);
      e.Graphics.DrawLine(allCharsPen, x1, allCharsTop, x2, allCharsTop);
      e.Graphics.DrawLine(allCharsPen, x1, allCharsBottom, x2, allCharsBottom);

      ObservedMetrics charMetrics = _getCharMetrics();

      // draw lines for just this char
      x2 -= 0.2f * TrueTypeCharPanel.Width;
      int thisCharTop = (int)charMetrics.CharTop;
      int thisCharBottom = (int)charMetrics.CharBottom;
      e.Graphics.DrawLine(thisCharPen, x1, thisCharTop, x2, thisCharTop);
      e.Graphics.DrawLine(thisCharPen, x1, thisCharBottom, x2, thisCharBottom);

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
      e.Graphics.DrawLine(thisCharPen, thisCharLeft, y1, thisCharLeft, y2);
      e.Graphics.DrawLine(thisCharPen, thisCharRight, y1, thisCharRight, y2);

      // draw labels last so that they cover up dashed lines
      e.Graphics.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
      using Font f = new Font("Arial", 8);
      e.Graphics.DrawLabel("ASCENT", f, new PointF(0, ascent), ascentColor, Color.DimGray);
      e.Graphics.DrawLabel("BASELINE", f, new PointF(0, baseline), baselineColor, Color.DimGray);
      e.Graphics.DrawLabel("DESCENT", f, new PointF(0, descent), descentColor, Color.DimGray);
      e.Graphics.DrawLabel("HEIGHT", f, new PointF(0, height), heightColor, Color.DimGray);

      int lastX = TrueTypeCharPanel.Width - 1;
      e.Graphics.DrawLabel("All CELLS", f, new PointF(lastX, allCellTop), cellsColor, Color.DimGray, LabelAlignment.Right);
      e.Graphics.DrawLabel("ALL CHARS", f, new PointF(lastX, allCharsTop), allCharsColor, Color.DimGray, LabelAlignment.Right);
      e.Graphics.DrawLabel("THIS CHAR", f, new PointF(lastX, thisCharTop), thisCharColor, Color.DimGray, LabelAlignment.Right);
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
      uint targetCharHeightPx = (uint)cellHeightUpDown.Value;

      _vlwFont = _builder.CreateFont(targetCharHeightPx, FontBuilderOptions);

      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
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
         e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
         //RectangleF srcRect = new(-0.5f, -0.5f, smallBitmap.Width + 0.5f, smallBitmap.Height + 0.5f);
         RectangleF srcRect = new(-0.5f, -0.5f, smallBitmap.Width + 1, smallBitmap.Height + 1);

         float pixelSize = 0.8f * VLWCharPanel.Height / (float)smallBitmap.Height;
         int x = (int)((VLWCharPanel.Width - pixelSize * smallBitmap.Width) / 2.0);
         int y = (int)((VLWCharPanel.Height - pixelSize * smallBitmap.Height) / 2.0);
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


   private void PreviewTextBox_TextChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
   }

   private void showGlyphsCheckBox_CheckedChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
   }

   private void magnificationUpDown_ValueChanged(object sender, EventArgs e)
   {
      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
   }

   private void charHeightUpDown_ValueChanged(object sender, EventArgs e)
   {
      VLWCharPanel.Invalidate();
      _createFont();
   }

   private void testObserveButton_Click(object sender, EventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      using Bitmap bitmap = new(TrueTypeCharPanel.Width, TrueTypeCharPanel.Height);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);

      float fontSizePx = FontUtil.GetFontSizePxForCellHeightPx(fontFamily, _getFontStyle(), 0.8f * TrueTypeCharPanel.Height);

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
      uint targetCharHeightPx = (uint)cellHeightUpDown.Value;
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
      uint targetCharHeightPx = (uint)cellHeightUpDown.Value;
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
      VLWPreviewPanel.Invalidate();
      TrueTypePreviewPanel.Invalidate();
      TrueTypeCharPanel.Invalidate();
      VLWCharPanel.Invalidate();
   }

   private void _DrawLines(Bitmap bitmap, int lineHeight)
   {
      using Graphics graphics = Graphics.FromImage(bitmap);

      using Pen pen = new Pen(Color.Red);
      pen.DashStyle = DashStyle.Dot;
      graphics.SmoothingMode = SmoothingMode.None;
      int line = 1;
      while (true)
      {
         float yL = line * lineHeight;

         if (yL > bitmap.Height)
         {
            break;
         }
         graphics.DrawLine(pen, 0, yL, bitmap.Width, yL);
         line++;
      }

   }
   private void VLWPreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      // create the bitmap that we will display
      using Bitmap bitmap = new(VLWPreviewPanel.Width, VLWPreviewPanel.Height);
      using Graphics graphics = Graphics.FromImage(bitmap);

      _DrawLines(bitmap, (int)cellHeightUpDown.Value);

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
                     if (c == '$')
                     {

                     }
                     int gx = x + glyph.gdX;
                     int gy = (int)(y + _vlwFont.Ascent - glyph.gdY);
                     //graphics.FillRectangle(Brushes.Green, new RectangleF(new Point(gx, gy), glyph.Bitmap.Size));
                     graphics.DrawImageUnscaled(glyph.Bitmap, gx, gy);
                  }
                  x += glyph.gxAdvance;
               }
               break;
         }
      }

      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      RectangleF srcRect = new(-0.5f, -0.5f, bitmap.Width + 1, bitmap.Height + 1);
      RectangleF dstRect = new(-0.5f, -0.5f, bitmap.Width * _magnification + 1, bitmap.Height * _magnification + 1);
      e.Graphics.DrawImage(bitmap, dstRect, srcRect, GraphicsUnit.Pixel);
   }

   private void TrueTypePreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      // create the bitmap that we will display
      using Bitmap bitmap = new(TrueTypePreviewPanel.Width, TrueTypePreviewPanel.Height);
      using Graphics graphics = Graphics.FromImage(bitmap);

      _DrawLines(bitmap, (int)cellHeightUpDown.Value);

      String str = PreviewTextBox.Text;

      FontFamily fontFamily = new FontFamily(FontComboBox.Text);
      int height = (int)cellHeightUpDown.Value;
      float fontSizePx = FontUtil.GetFontSizePxForCellHeightPx(fontFamily, _getFontStyle(), height);
      Font font = new Font(
         fontFamily,
         fontSizePx,
         _getFontStyle(),
         GraphicsUnit.Pixel);
      graphics.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
      //graphics.Clear(Color.Black);
      graphics.DrawString(str, font, Brushes.White, new Point(0, 0));


      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      RectangleF srcRect = new(-0.5f, -0.5f, bitmap.Width + 1, bitmap.Height + 1);
      RectangleF dstRect = new(-0.5f, -0.5f, bitmap.Width * _magnification + 1, bitmap.Height * _magnification + 1);
      e.Graphics.DrawImage(bitmap, dstRect, srcRect, GraphicsUnit.Pixel);
   }
}
