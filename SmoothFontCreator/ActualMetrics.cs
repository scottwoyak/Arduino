using System.Diagnostics;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace SmoothFontCreator;

class ActualMetrics
{
   public Metric CellMetric = new();
   public Metric CharMetric = new();

   public float Height => CharMetric.Height;
   public float Width => CharMetric.Width;
   public float LineSpacing => CellMetric.Height;
   public float Top => CharMetric.yMin - CellMetric.yMin;
   public float Bottom => Top + Height;

   public double ElapsedMs { get; private set; }

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
      for (int y = 0; y < b.Height; y++)
      {
         for (int x = 0; x < b.Width; x++)
         {
            int index = (int)(y * bmpData.Stride + x * 4); // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];

            if (A != 0)
            {
               CellMetric.update(x, y);

               byte R = argbValues[index + 2];
               //byte G = argbValues[index + 1];
               //byte B = argbValues[index + 0];
               //if (R!=0 ||B!= 0 ||G!=0)
               if (R!=0)
               {
                  CharMetric.update(x, y);
                  break;
               }
            }
         }

         for (int x = b.Width - 1; x >= CellMetric.xMax; x--)
         {
            int index = (int)(y * bmpData.Stride + x * 4); // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];

            if (A != 0)
            {
               CellMetric.update(x, y);

               byte R = argbValues[index + 2];
               //byte G = argbValues[index + 1];
               //byte B = argbValues[index + 0];
               //if (R!=0 ||B!= 0 ||G!=0)
               if (R != 0)
               {
                  CharMetric.update(x, y);
                  break;
               }
            }
         }
      }

      b.UnlockBits(bmpData); // Mandatory

      ElapsedMs = sw.Elapsed.TotalMilliseconds;

      Debug.WriteLine($"Bitmap Size: {b.Width}px X {b.Height}px");
      Debug.WriteLine($"Cell Size: {CellMetric.Width}px X {CellMetric.Height}px");
      Debug.WriteLine($"Char Size: {CharMetric.Width}px X {CharMetric.Height}px");
   }
}
