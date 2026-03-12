using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace SmoothFontCreator;

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
      CharPanel.MouseClick += (s, e) =>
      {
         CharPanel.Invalidate();
      };
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

         ActualMetrics actualMetrics = new(bitmap);
         MsgLabel.Text = actualMetrics.ElapsedMs.ToString("0.0ms");

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

         float top = pt.Y + actualMetrics.Top;
         float bottom = pt.Y + actualMetrics.Bottom;
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

   private void button1_Click(object sender, EventArgs e)
   {
      byte[] bytes = File.ReadAllBytes(@"C:\SourceCode\Arduino\SmoothFontCreator\Roboto32.vlw");

      Stopwatch sw = Stopwatch.StartNew();
      VLWContent content = new(bytes);

      Debug.WriteLine($"Bytes read in {sw.Elapsed.TotalMilliseconds} ms");

      sw.Restart();
      byte[] outBytes = content.ToByteArray();
      Debug.WriteLine($"Bytes created in {sw.Elapsed.TotalMilliseconds} ms");

      for (int i = 0; i < bytes.Length; i++)
      {
         if (bytes[i] != outBytes[i])
         {
            Debugger.Break();
         }
      }
   }
}
