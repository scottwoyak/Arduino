using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace SmoothFontCreator;

class Metric
{
   public uint xMin = uint.MaxValue;
   public uint xMax = uint.MinValue;
   public uint yMin = uint.MaxValue;
   public uint yMax = uint.MinValue;
   public uint Width => xMax - xMin;
   public uint Height => yMax - yMin;

   public void update(uint x, uint y)
   {
      xMin = uint.Min(xMin, x);
      xMax = uint.Max(xMax, x);
      yMin = uint.Min(yMin, y);
      yMax = uint.Max(yMax, y);
   }
}

class ActualMetrics
{
   public Metric CellMetric = new();
   public Metric CharMetric = new();

   public float Height => CharMetric.Height;
   public float Width => CharMetric.Width;
   public float LineSpacing => CellMetric.Height;
   public float Top => CharMetric.yMin - CellMetric.yMin;
   public float Bottom => Top + Height;

   public ActualMetrics(Bitmap b)
   {
      Stopwatch sw = Stopwatch.StartNew();

      /*
      for (uint x = 0; x < b.Width; x++)
      {
         for (uint y = 0; y < b.Height; y++)
         {
            Color pixelColor = b.GetPixel((int)x, (int)y);
            if (pixelColor.A != 0)
            {
               CellMetric.update(x, y);
               if (pixelColor.ToArgb() != Color.Black.ToArgb())
               {
                  CharMetric.update(x, y);
               }
            }
         }
      }
      */

      Rectangle rect = new(0, 0, b.Width, b.Height);
      BitmapData bmpData = b.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);

      // Get address of the first scan line
      IntPtr ptr = bmpData.Scan0;

      // Calculate total bytes; Stride handles row padding
      int bytes = Math.Abs(bmpData.Stride) * b.Height;
      byte[] argbValues = new byte[bytes];

      // Copy RGB values to array
      Marshal.Copy(ptr, argbValues, 0, bytes);

      // scan pixel data (ARGB format)
      for (uint x = 0; x < b.Width; x++)
      {
         for (uint y = 0; y < b.Height; y++)
         {
            int index = (int)(y * bmpData.Stride + x * 4); // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            byte R = argbValues[index + 2];
            byte G = argbValues[index + 1];
            byte B = argbValues[index + 0];
            Color pixelColor = Color.FromArgb(A, R, G, B);

            if (pixelColor.A != 0)
            {
               CellMetric.update(x, y);
               if (pixelColor.ToArgb() != Color.Black.ToArgb())
               {
                  CharMetric.update(x, y);
               }
            }
         }
      }

      b.UnlockBits(bmpData); // Mandatory

      Console.WriteLine($"Metrics Computation Time: {sw.ElapsedMilliseconds}ms");

      Debug.WriteLine($"Bitmap Size: {b.Width}px X {b.Height}px");
      Debug.WriteLine($"Cell Size: {CellMetric.Width}px X {CellMetric.Height}px");
      Debug.WriteLine($"Char Size: {CharMetric.Width}px X {CharMetric.Height}px");
   }
}

class GdiMetrics
{
   public float Height;
   public float Width;
   public float Ascent;
   public float Descent;
   public float LineSpacing;
   public float Top;
   public float Baseline;
   public float Bottom;

   public GdiMetrics(Font font, Graphics g)
   {
      float heightEm = font.FontFamily.GetEmHeight(FontStyle.Regular);
      float ascentEm = font.FontFamily.GetCellAscent(FontStyle.Regular);
      float descentEm = font.FontFamily.GetCellDescent(FontStyle.Regular);
      float lineSpacingEm = font.FontFamily.GetLineSpacing(FontStyle.Regular);

      Height = font.GetHeight(g);
      Width = TextRenderer.MeasureText("0", font, new Size(1000, 1000), TextFormatFlags.NoPadding).Width;
      Ascent = (ascentEm / lineSpacingEm) * Height;
      Descent = (descentEm / lineSpacingEm) * Height;
      LineSpacing = (Height / heightEm) * lineSpacingEm;

      Top = Height - font.Size;
      Baseline = Ascent;
      Bottom = Ascent + Descent;
   }
}

public partial class MainForm : Form
{

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

      TestCharsTextBox.TextChanged += (s, e) =>
      {
         CharPanel.Invalidate();
         MetricsPanel.Invalidate();
      };

      CharPanel.MouseWheel += CharPanel_MouseWheel;
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
      ASCIILabel.Text = ((int)c).ToString();
      HexLabel.Text = $"0x{(int)c:X2}";

      CharPanel.Invalidate();
   }

   private void FontComboBox_SelectedValueChanged(object sender, EventArgs e)
   {
      AllCharsPanel.Invalidate();
      CharPanel.Invalidate();
      MetricsPanel.Invalidate();
   }

   private void PrintFontMetrics(Font font, Graphics g)
   {
      GdiMetrics gdiMetrics = new(font, g);

      Debug.WriteLine($"{font.FontFamily.GetName(0)}, {font.SizeInPoints}pt");
      Debug.WriteLine($"font.Size: {font.Size}");
      Debug.WriteLine($"font.Height: {font.Height}");
      Debug.WriteLine($"font.GetHeight(Graphics): {font.GetHeight(g)}");
      Debug.WriteLine("");

      Debug.WriteLine($"Height: {gdiMetrics.Height}px");
      Debug.WriteLine($"Width: {gdiMetrics.Width}px");
      Debug.WriteLine($"Ascent: {gdiMetrics.Ascent}px");
      Debug.WriteLine($"Descent: {gdiMetrics.Descent}px");
      Debug.WriteLine($"LineSpacing: {gdiMetrics.LineSpacing}px");
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

      e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
      TextRenderer.DrawText(e.Graphics, chars, font, new Point(0, 0), Color.White, Color.Transparent, TextFormatFlags.NoPadding);
   }

   private void CharPanel_Paint(object sender, PaintEventArgs e)
   {
      if (TestCharTextBox.Text.Length > 0)
      {
         using Bitmap bitmap = new(CharPanel.Width, CharPanel.Height, PixelFormat.Format32bppArgb);
         Graphics g = Graphics.FromImage(bitmap);
         g.Clear(Color.Transparent);

         FontFamily fontFamily = new(FontComboBox.Text);
         Font font = new Font(
            fontFamily,
            512,
            FontStyle.Regular,
            GraphicsUnit.Pixel);

         PrintFontMetrics(font, g);

         string testChar = TestCharTextBox.Text.Substring(0, 1);
         float heightPx = font.GetHeight(e.Graphics);
         float widthPx = TextRenderer.MeasureText(testChar, font, new Size(1000, 1000), TextFormatFlags.NoPadding).Width;

         g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
         Point pt = new((int)(CharPanel.Width - widthPx) / 2, (int)(CharPanel.Height - heightPx) / 2);
         TextRenderer.DrawText(g, testChar, font, pt, Color.White, Color.Black, TextFormatFlags.NoPadding);

         e.Graphics.DrawImage(bitmap, new Point(0, 0));

         ActualMetrics computedMetrics = new(bitmap);

         // draw what Windows reports on the left
         GdiMetrics gdiMetrics = new GdiMetrics(font, g);

         Pen topPen = new(Color.Pink, 1);
         Pen baselinePen = new(Color.LightCoral, 1);
         Pen bottomPen = new(Color.Cyan, 1);
         topPen.DashStyle = DashStyle.Dot;
         baselinePen.DashStyle = DashStyle.Dot;
         bottomPen.DashStyle = DashStyle.Dot;

         float x1 = 0;
         float x2 = CharPanel.Width / 2;
         e.Graphics.DrawLine(topPen, x1, pt.Y + gdiMetrics.Top, x2, pt.Y + gdiMetrics.Top);
         e.Graphics.DrawLine(baselinePen, x1, pt.Y + gdiMetrics.Baseline, x2, pt.Y + gdiMetrics.Baseline);
         e.Graphics.DrawLine(bottomPen, x1, pt.Y + gdiMetrics.Bottom, x2, pt.Y + gdiMetrics.Bottom);

         float top = pt.Y + computedMetrics.Top;
         float bottom = pt.Y + computedMetrics.Bottom;
         x1 = CharPanel.Width / 2;
         x2 = CharPanel.Width;

         // draw what we calculated on the right
         e.Graphics.DrawLine(topPen, x1, top, x2, top);
         e.Graphics.DrawLine(bottomPen, x1, bottom, x2, bottom);
      }
   }

   private void MetricsPanel_Paint(object sender, PaintEventArgs e)
   {
      FontFamily fontFamily = new FontFamily(FontComboBox.Text);
      Font font = new Font(
         fontFamily,
         128,
         FontStyle.Regular,
         GraphicsUnit.Pixel);



      e.Graphics.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
      TextRenderer.DrawText(e.Graphics, TestCharsTextBox.Text, font, new Point(0, 0), Color.White, Color.Black, TextFormatFlags.NoPadding);

      GdiMetrics gdiMetrics = new(font, e.Graphics);
      float top = gdiMetrics.Top;
      float baseline = gdiMetrics.Baseline;
      float bottom = gdiMetrics.Bottom;
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
}
