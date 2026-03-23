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

   public double CharHeight => CharRect.Height;
   public double CharWidth => CharRect.Width;
   public double CharLeft => CharRect.Left;
   public double CharRight => CharRect.Right;
   public double CharTop => CharRect.Top;
   public double CharBottom => CharRect.Bottom;

   public double TopMargin => CharRect.Top - CellRect.Top;
   public double BottomMargin => CellRect.Bottom - CharRect.Bottom;
   public double LeftMargin => CharRect.Left - CellRect.Left;
   public double RightMargin => CellRect.Right - CharRect.Right;

   public String CellRectString => CellRect.ToString();
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
      return desiredHeightPx * ((double)Font.Size / CharRect.Height);
   }

   private uint _FullScan(BitmapData bmpData, byte[] argbValues)
   {
      uint count = 0;
      for (int y = 0; y < bmpData.Height; y++)
      {
         for (int x = 0; x < bmpData.Width; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            count++;

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
               }
            }
         }
      }
      return count;
   }

   private uint _OptimizedScan(BitmapData bmpData, byte[] argbValues)
   {
      uint count = 0;

      // first do a scan to find the cell bounds
      int xM = bmpData.Width / 2;
      int yM = bmpData.Height / 2;

      // left and right
      for (int x = 0; x < bmpData.Width; x++)
      {
         int index = yM * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];
         count++;

         if (A != 0)
         {
            CellRect.Update(x, yM);
            break;
         }
      }

      for (int x = bmpData.Width - 1; x > CellRect.Left; x--)
      {
         int index = yM * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];
         count++;

         if (A != 0)
         {
            CellRect.Update(x, yM);
            break;
         }
      }

      // top and bottom
      for (int y = 0; y < bmpData.Height; y++)
      {
         int index = y * bmpData.Stride + xM * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];
         count++;

         if (A != 0)
         {
            CellRect.Update(xM, y);
            break;
         }
      }

      for (int y = bmpData.Height - 1; y > CellRect.Top; y--)
      {
         int index = y * bmpData.Stride + xM * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];
         count++;

         if (A != 0)
         {
            CellRect.Update(xM, y);
            break;
         }
      }


      // then do scan lines

      // top to bottom
      bool found = false;
      for (int y = (int)CellRect.Top; y <= CellRect.Bottom && found == false; y++)
      {
         for (int x = (int)CellRect.Left; x <= CellRect.Right && found == false; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            count++;

            byte R = argbValues[index + 2];
            if (R != 0)
            {
               CharRect.Update(x, y);
               found = true;
            }
         }
      }
      
      // bottom to top
      found = false;
      for (int y = (int)CellRect.Bottom; y >= CellRect.Top && found == false; y--)
      {
         for (int x = (int)CellRect.Left; x <= CellRect.Right && found == false; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            count++;

            byte R = argbValues[index + 2];
            if (R != 0)
            {
               CharRect.Update(x, y);
               found = true;
            }
         }
      }

      // left to right
      found = false;
      for (int x = (int)CellRect.Left; x <= CellRect.Right && found == false; x++)
      {
         for (int y = (int)CellRect.Top; y <= CellRect.Bottom && found == false; y++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            count++;

            byte R = argbValues[index + 2];
            if (R != 0)
            {
               CharRect.Update(x, y);
               found = true;
            }
         }
      }

      // right to left
      found = false;
      for (int x = (int)CellRect.Right; x >= CellRect.Left && found == false; x--)
      {
         for (int y = (int)CellRect.Top; y <= CellRect.Bottom && found == false; y++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            count++;

            byte R = argbValues[index + 2];
            if (R != 0)
            {
               CharRect.Update(x, y);
               found = true;
            }
         }
      }

      return count;
   }


   private uint _OptimizedScan2(BitmapData bmpData, byte[] argbValues)
   {
      // first do a scan to find the cell bounds by using a single line in the middle

      // then do scan lines
      uint count = 0;

      bool done = false;

      // top to bottom
      for (int y = 0; y < bmpData.Height; y++)
      {
         for (int x = 0; x < bmpData.Width; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            count++;

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
                  done = true;
                  break;
               }
            }
         }

         if (done == true)
         {
            break;
         }
      }

      // bottom to top
      done = false;
      for (int y = bmpData.Height - 1; y > CharRect.Bottom; y--)
      {
         for (int x = 0; x < bmpData.Width; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            count++;

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
                  done = true;
                  break;
               }
            }
         }

         if (done == true)
         {
            break;
         }
      }

      // left to right
      done = false;
      for (int x = bmpData.Width - 1; x >= 0; x--)
      {
         for (int y = (int)CharRect.Top; y < CharRect.Bottom; y++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            count++;

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
                  done = true;
                  break;
               }
            }
         }

         if (done == true)
         {
            break;
         }
      }

      // right to left
      done = false;
      for (int x = 0; x < bmpData.Width; x++)
      {
         for (int y = (int)CharRect.Top; y < CharRect.Bottom; y++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];
            count++;

            if (A != 0)
            {
               CellRect.Update(x, y);

               byte R = argbValues[index + 2];
               if (R != 0)
               {
                  CharRect.Update(x, y);
                  done = true;
                  break;
               }
            }
         }

         if (done == true)
         {
            break;
         }
      }

      return count;
   }

   private void _scan(Bitmap b)
   {
      Rectangle rect = new(0, 0, b.Width, b.Height);
      BitmapData bmpData = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);

      // Get address of the first scan line
      IntPtr ptr = bmpData.Scan0;

      // Calculate total bytes; Stride handles row padding
      int bytes = Math.Abs(bmpData.Stride) * b.Height;
      byte[] argbValues = new byte[bytes];

      // Copy RGB values to array
      Marshal.Copy(ptr, argbValues, 0, bytes);

      //uint count = _FullScan(bmpData, argbValues);
      uint count = _OptimizedScan(bmpData, argbValues);

      //Debug.WriteLine($"{count} comparisons");
      //Debug.WriteLine($"  Bitmap: {bmpData.Width} {bmpData.Height}");
      //Debug.WriteLine($"  Cell: {CellRect}");
      //Debug.WriteLine($"  Char: {CharRect}");

      b.UnlockBits(bmpData); // Mandatory
   }

   public void Expand(ObservedMetrics other)
   {
      CellRect.Intersect(other.CellRect);
      CharRect.Intersect(other.CharRect);
   }

   public ObservedMetrics WithCharHeight(double desiredCharHeight)
   {
      double scaleFactor = desiredCharHeight / CharHeight;
      return this.WithScaleFactor(scaleFactor);
   }

   public ObservedMetrics WithScaleFactor(double scaleFactor)
   {
      Font scaledFont = new Font(Font.FontFamily, (float)(scaleFactor * Font.GetHeight()), GraphicsUnit.Pixel);
      ObservedMetrics scaledMetrics = new(scaledFont);

      scaledMetrics.CharRect.Left = scaleFactor * CharRect.Left;
      scaledMetrics.CharRect.Right = scaleFactor * CharRect.Right;
      scaledMetrics.CharRect.Top = scaleFactor * CharRect.Top;
      scaledMetrics.CharRect.Bottom = scaleFactor * CharRect.Bottom;

      scaledMetrics.CellRect.Left = scaleFactor * CellRect.Left;
      scaledMetrics.CellRect.Right = scaleFactor * CellRect.Right;
      scaledMetrics.CellRect.Top = scaleFactor * CellRect.Top;
      scaledMetrics.CellRect.Bottom = scaleFactor * CellRect.Bottom;

      return scaledMetrics;
   }
}
