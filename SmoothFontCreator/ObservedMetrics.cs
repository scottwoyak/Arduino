using System.Diagnostics;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace SmoothFontCreator;

public class ObservedMetrics
{
   private FontRect CellRect = new();
   private FontRect CharRect = new();
   public Font Font { get; private set; }

   public double CellHeight => CellRect.Height;
   public double CellWidth => CellRect.Width;
   public double CellLeft => CellRect.Left;
   public double CellRight => CellRect.Right;
   public double CellTop => CellRect.Top;
   public double CellBottom => CellRect.Bottom;
   public String CellRectString => CellRect.ToString();

   public double CharHeight => CharRect.Height;
   public double CharWidth => CharRect.Width;
   public double CharLeft => CharRect.Left;
   public double CharRight => CharRect.Right;
   public double CharTop => CharRect.Top;
   public double CharBottom => CharRect.Bottom;
   public String CharRectString => CharRect.ToString();


   public ObservedMetrics(Font font)
   {
      Font = font;
   }

   public ObservedMetrics(Bitmap b, Font font)
   {
      Font = font;
      _scan(b);
   }

   public double RealPxToFontSizePx(double desiredHeightPx)
   {
      return desiredHeightPx * ((double) Font.Size / CharRect.Height);
   }

   public double TopMarginPxForFontSizePx(double fontSizePx)
   {
      return (CharRect.Top - CellRect.Top) * (fontSizePx / Font.Size);
   }

   public double LeftMarginPxForFontSizePx(double fontSizePx)
   {
      return (CharRect.Left - CellRect.Left) * (fontSizePx / Font.Size);
   }

   public double WidthPxForFontSizePx(double fontSizePx)
   {
      return CharRect.Width * (fontSizePx / Font.Size);
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
   }

   public void Expand(ObservedMetrics other)
   {
      CellRect.Intersect(other.CellRect);
      CharRect.Intersect(other.CharRect);
   }
}
