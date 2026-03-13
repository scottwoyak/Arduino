using System.Diagnostics;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace SmoothFontCreator;

public class ObservedMetrics
{
   public Font Font;
   public FontRect CellRect = new();
   public FontRect CharRect = new();


   public double ElapsedMs { get; private set; }

   public ObservedMetrics(Font font)
   {
      Font = font;
   }

   public ObservedMetrics(Bitmap b, Font font)
   {
      Font = font;
      _scan(b);
   }

   private void _scan(Bitmap b)
   { 
      Stopwatch sw = Stopwatch.StartNew();

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
      for (int y = 0; y < b.Height; y++)
      {
         for (int x = 0; x < b.Width; x++)
         {
            int index = (int)(y * bmpData.Stride + x * 4); // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
                  //break;
               }
            }
         }

         /*
         for (int x = b.Width - 1; x >= CellMetric.xMax; x--)
         {
            int index = (int)(y * bmpData.Stride + x * 4); // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];

            if (A != 0)
            {
               CellMetric.update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharMetric.update(x, y);
                  break;
               }
            }
         }
         */
      }

      b.UnlockBits(bmpData); // Mandatory

      ElapsedMs = sw.Elapsed.TotalMilliseconds;
   }

   public void Expand(ObservedMetrics other)
   {
      CellRect.Intersect(other.CellRect);
      CharRect.Intersect(other.CharRect);
   }

   /*
   public static ObservedMetrics FromBitmap(Bitmap b)
   {
      return new ObservedMetrics(b);
   }

   public static ObservedSize ForAllChars(FontFamily fontFamily)
   {
      // first figure out the font size that will fill the pixel height
      Font font = new Font(
         fontFamily,
         100,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      const int testSizePx = 1000;
      using Bitmap bitmap = new(testSizePx, testSizePx);
      using Graphics graphics = Graphics.FromImage(bitmap);
      float heightPx = font.GetHeight(graphics);
      float pxSize = 0.8f*100 * testSizePx / heightPx;

      // now write all the chars to the bitmap and observe the metrics
      font = new Font(
         fontFamily,
         pxSize,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      graphics.Clear(Color.Transparent);
      Point pt = new((int)(0.1 * testSizePx), (int)(0.1 * testSizePx)); // provide an offset to we can capture content outside of the cell
      for (char c = (char)0x21; c < 0x7e; c++)
      {
         graphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);
      }

      ObservedMetrics observedMetrics = new(bitmap);

      // scale everything to 0-1
      return new ObservedSize(observedMetrics,(int) (bitmap.Height));
   }

   public static ObservedSize ForChar(FontFamily fontFamily, char c)
   {
      // first figure out the font size that will fill the pixel height
      Font font = new Font(
         fontFamily,
         100,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      const int testSizePx = 1000;
      using Bitmap bitmap = new(testSizePx, testSizePx);
      using Graphics bitmapGraphics = Graphics.FromImage(bitmap);
      float heightPx = font.GetHeight(bitmapGraphics);
      float pxSize = 0.8f* 100 * testSizePx / heightPx;

      // now write all the chars to the bitmap and observe the metrics
      font = new Font(
         fontFamily,
         pxSize,
         FontStyle.Regular,
         GraphicsUnit.Pixel);

      bitmapGraphics.Clear(Color.Transparent);
      Point pt = new((int)(0.1 * testSizePx), (int)(0.1 * testSizePx)); // provide an offset to we can capture content outside of the cell
      bitmapGraphics.DrawPreciseString(c.ToString(), font, pt, Color.White, Color.Black);

      ObservedMetrics observedMetrics = new(bitmap);

      // scale everything to 0-1
      return new ObservedSize(observedMetrics, testSizePx);
   }
   */
}
