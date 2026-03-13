using System.Diagnostics;
using System.Drawing.Drawing2D;

namespace SmoothFontCreator;

public partial class MainForm : Form
{
   private ObservedMetrics _allCharsObservedMetrics;
   private Dictionary<char, ObservedMetrics> _charMetrics = [];

   public MainForm()
   {
      InitializeComponent();

      FontComboBox.Items.Add("Arial");
      FontComboBox.Items.Add("Times New Roman");
      FontComboBox.Items.Add("Roboto Mono");
      FontComboBox.Items.Add("Roboto");
      FontComboBox.SelectedIndex = 0;
      FontComboBox.SelectedValueChanged += FontComboBox_SelectedValueChanged;


      CharPanel.Paint += CharPanel_Paint;
      AllCharsPanel.Paint += AllCharsPanel_Paint;
      MetricsPanel.Paint += MetricsPanel_Paint;
      CharPreviewPanel.Paint += CharPreviewPanel_Paint;

      TestCharsTextBox.TextChanged += (s, e) =>
      {
         MetricsPanel.Invalidate();
      };

      CharPanel.MouseWheel += CharPanel_MouseWheel;

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

      Debug.WriteLine($"{sw.ElapsedMilliseconds}ms");
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

      CharPanel.Invalidate();
      CharPreviewPanel.Invalidate();
   }

   private void FontComboBox_SelectedValueChanged(object sender, EventArgs e)
   {
      _UpdateMetrics();

      CharPanel.Invalidate();
      CharPreviewPanel.Invalidate();
      AllCharsPanel.Invalidate();
      MetricsPanel.Invalidate();
   }

   private void AllCharsPanel_Paint(object sender, PaintEventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      Font font = new Font(
         fontFamily,
         32,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      string chars = "abcdefghijklmnopqrstuvwxyz\nABCDEFGHIJKLMNOPQRSTUVWXYZ\n0123456789!@#$%^&*()_+-=~`[]{}|;:'\",.<>/?";

      e.Graphics.DrawPreciseString(chars, font, new Point(0, 0), Color.White, Color.Transparent);
   }

   private void CharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0)
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
      Color allCharsColor = Color.LightYellow;
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

      float allCellTop = _allCharsObservedMetrics.CellRect.Top;
      float allCharTop = _allCharsObservedMetrics.CharRect.Top;
      float allCharBottom = _allCharsObservedMetrics.CharRect.Bottom;
      float allCellBottom = _allCharsObservedMetrics.CellRect.Bottom;

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
      float oneCharTop = oneCharObservedMetrics.CharRect.Top;
      float oneCharBottom = oneCharObservedMetrics.CharRect.Bottom;
      e.Graphics.DrawLine(oneCharPen, x1, oneCharTop, x2, oneCharTop);
      e.Graphics.DrawLine(oneCharPen, x1, oneCharBottom, x2, oneCharBottom);

      Debug.WriteLine($"CharPanel Paint: {sw.Elapsed.TotalMilliseconds}");
   }

   private void CharPreviewPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length == 0)
      {
         return;
      }

      if (int.TryParse(DesiredHeightTextBox.Text, out int fontSizePx) == false)
      {
         return;
      }

      FontFamily fontFamily = new FontFamily(FontComboBox.Text);

      // create a very large font and then scale it down to what we want to display
      using Bitmap largeBitmap = new Bitmap(2048, 2048);
      using Graphics largeGraphics = Graphics.FromImage(largeBitmap);

      // figure out the font size needed to fill the bitmap
      float scaledCharsHeight = _allCharsObservedMetrics.CharRect.Height/_allCharsObservedMetrics.Font.Size;
      float scaledTopMargin = (_allCharsObservedMetrics.CharRect.Top - _allCharsObservedMetrics.CellRect.Top) /_allCharsObservedMetrics.Font.GetHeight();

      float neededFontSizePx = 2048*scaledCharsHeight;
      float topMargin = scaledTopMargin * neededFontSizePx;

      Font font = new Font(
         fontFamily,
         neededFontSizePx,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      string testChar = TestCharTextBox.Text.Substring(0, 1);
      /*
      float heightPx = font.GetHeight(e.Graphics);
      float widthPx = TextRenderer.MeasureText(testChar, font, new Size(10000, 10000), TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
      */

      //Point pt = new((int)(CharPanel.Width - widthPx) / 2, (int)(CharPanel.Height - heightPx) / 2);
      Point pt = new(0, (int)-topMargin);
      //Point pt = new(0, 0);
      largeGraphics.DrawPreciseString(testChar, font, pt, Color.White, Color.Black);
      ObservedMetrics om = new(largeBitmap, font);
      //largeGraphics.Clear(Color.Black);

      // scale the full bitmap down to the smaller one
      int smallHeightPx = int.Parse(DesiredHeightTextBox.Text);
      int smallWidthPx = smallHeightPx; // (int)Math.Ceiling(fontSizePx * ((float)_allCharsObservedMetrics.CharMetric.Width / _allCharsObservedMetrics.CharMetric.Height));
      using Bitmap smallBitmap = new Bitmap(smallWidthPx, smallHeightPx);
      using Graphics smallGraphics = Graphics.FromImage(smallBitmap);

      smallGraphics.InterpolationMode = InterpolationMode.Default;
      Rectangle srcRect = new(0, 0, largeBitmap.Width, largeBitmap.Height);
      Rectangle dstRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);
      smallGraphics.Clear(Color.Pink);
      smallGraphics.DrawImage(largeBitmap, dstRect, srcRect, GraphicsUnit.Pixel);

      // draw the bitmap to the preview char panel
      //e.Graphics.DrawImageUnscaled(smallBitmap, 10, 10);
      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      srcRect = new(0, 0, smallBitmap.Width, smallBitmap.Height);
      dstRect = new(0, 0, (int)(CharPreviewPanel.Height * ((float)smallBitmap.Width / smallBitmap.Height)), CharPreviewPanel.Height);
      e.Graphics.DrawImage(smallBitmap, dstRect, srcRect, GraphicsUnit.Pixel);

      /*
      ObservedSize singleMetrics = ObservedMetrics.ForChar(fontFamily, TestCharTextBox.Text[0]);

      // first draw a bitmap in the desired size
      int heightPx = fontSizePx;
      int widthPx = (int)Math.Ceiling(fontSizePx * (_allCharsObservedSize.CharRect.Width / _allCharsObservedSize.CharRect.Height));

      using Bitmap bitmap = new(widthPx, heightPx);
      using Graphics graphics = Graphics.FromImage(bitmap);

      float fontSizePt = (float)(fontSizePx / _allCharsObservedSize.CellRect.Height);

      Font font = new Font(
         fontFamily,
         fontSizePt,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      graphics.Clear(Color.Transparent);

      // fill the background with a checkerboard pattern to make it easier to see the edges of the character
      for (int x = 0; x < bitmap.Width; x++)
      {
         for (int y = 0; y < bitmap.Height; y++)
         {
            if ((x + y) % 2 == 0)
            {
               bitmap.SetPixel(x, y, Color.FromArgb(10, 255, 255, 255)); // light gray
            }
         }
      }

      double cellWidthPx = fontSizePx * singleMetrics.CellRect.Width;
      double cellHeightPx = fontSizePx * singleMetrics.CellRect.Height;
      double topMargin = Math.Round(fontSizePx * (_allCharsObservedSize.CharRect.Top - _allCharsObservedSize.CellRect.Top));
      Point topLeft = new((int)((bitmap.Width - cellWidthPx) / 2), (int)-topMargin);

      Debug.WriteLine($"Adjusting to draw char at ({topLeft.X},{topLeft.Y})");
      graphics.DrawPreciseString(TestCharTextBox.Text, font, topLeft, Color.White, Color.Transparent);

      // then draw the bitmap to the preview char panel
      e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
      Rectangle srcRect = new(0, 0, bitmap.Width, bitmap.Height);
      Rectangle dstRect = new(0, 0, (int)(CharPreviewPanel.Height * ((float)bitmap.Width / bitmap.Height)), CharPreviewPanel.Height);
      e.Graphics.DrawImage(bitmap, dstRect, srcRect, GraphicsUnit.Pixel);
      */
   }

   private void MetricsPanel_Paint(object sender, PaintEventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);
      Font font = new Font(
         fontFamily,
         128,
         FontStyle.Regular,
         GraphicsUnit.Pixel);



      e.Graphics.DrawPreciseString(TestCharsTextBox.Text, font, new Point(0, 0), Color.White, Color.Black);

      GdiMetrics gdiMetrics = font.GetGdiMetrics();
      float top = gdiMetrics.TopPx;
      float baseline = gdiMetrics.BaselinePx;
      float bottom = gdiMetrics.BottomPx;
      Pen topPen = new(Color.Pink, 1);
      Pen baselinePen = new(Color.LightCoral, 1);
      Pen bottomPen = new(Color.Cyan, 1);
      topPen.DashStyle = DashStyle.Dot;
      baselinePen.DashStyle = DashStyle.Dot;
      bottomPen.DashStyle = DashStyle.Dot;

      for (int i = 0; i < 8; i++)
      {
         float start = i * (bottom);
         e.Graphics.DrawLine(topPen, 0, start + top, MetricsPanel.Width, start + top);
         e.Graphics.DrawLine(baselinePen, 0, start + baseline, MetricsPanel.Width, start + baseline);
         e.Graphics.DrawLine(bottomPen, 0, start + bottom, MetricsPanel.Width, start + bottom);
      }
   }

   private void button1_Click(object sender, EventArgs e)
   {
      /*
      byte[] bytes = File.ReadAllBytes(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");

      // process a byte stream
      VLWContent content = new(bytes);

      // create a byte stream
      byte[] outBytes = content.ToByteArray();

      // do a check to make sure they're the same
      for (int i = 0; i < bytes.Length; i++)
      {
         if (bytes[i] != outBytes[i])
         {
            Debugger.Break();
         }
      }
      */

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
}
