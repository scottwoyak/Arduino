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


   public double Baseline
   {
      get
      {
         GdiMetrics metrics = new GdiMetrics(Font);
         return metrics.BaselinePx;
      }
   }

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

   private void _ScanForCellBounds(BitmapData bmpData, byte[] argbValues)
   {
      // left and right

      // test at the top, middle and bottom
      int[] yTests = { 0, bmpData.Height / 2, bmpData.Height - 1 };

      bool found = false;
      foreach (int yT in yTests)
      {
         for (int x = 0; x < bmpData.Width; x++)
         {
            int index = yT * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
            byte A = argbValues[index + 3];

            if (A != 0)
            {
               CellRect.Update(x, yT);
               found = true;
               break;
            }
         }

         if (found)
         {
            for (int x = bmpData.Width - 1; x > CellRect.Left; x--)
            {
               int index = yT * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)
               byte A = argbValues[index + 3];

               if (A != 0)
               {
                  CellRect.Update(x, yT);
                  break;
               }
            }
         }

         if (found)
         {
            break;
         }
      }

      // top and bottom
      int xT = (int) (CellRect.Left + CellRect.Right)/2;
      for (int y = 0; y < bmpData.Height; y++)
      {
         int index = y * bmpData.Stride + xT * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];

         if (A != 0)
         {
            CellRect.Update(xT, y);
            break;
         }
      }

      for (int y = bmpData.Height - 1; y > CellRect.Top; y--)
      {
         int index = y * bmpData.Stride + xT * 4; // 4 bytes per pixel (ARGB)
         byte A = argbValues[index + 3];

         if (A != 0)
         {
            CellRect.Update(xT, y);
            break;
         }
      }
   }

   private void _ScanForCharBounds(BitmapData bmpData, byte[] argbValues)
   {
      // top to bottom
      bool found = false;
      for (int y = (int)CellRect.Top; y <= CellRect.Bottom && found == false; y++)
      {
         for (int x = (int)CellRect.Left; x <= CellRect.Right && found == false; x++)
         {
            int index = y * bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)

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

            byte R = argbValues[index + 2];
            if (R != 0)
            {
               CharRect.Update(x, y);
               found = true;
            }
         }
      }
   }

   private void _OptimizedScan(BitmapData bmpData, byte[] argbValues)
   {
      // first scan for the cell boundaries
      _ScanForCellBounds(bmpData, argbValues);

      // then scan for the char bounds within the cell
      _ScanForCharBounds(bmpData, argbValues);
   }

   private void _scan(Bitmap b)
   {
      Rectangle rect = new(0, 0, b.Width, b.Height);
      BitmapData bmpData = b.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);

      // Get address of the first scan line
      IntPtr ptr = bmpData.Scan0;

      // Calculate total bytes; Stride handles row padding
      int bytes = Math.Abs(bmpData.Stride) * bmpData.Height;
      byte[] argbValues = new byte[bytes];

      // Copy RGB values to array
      Marshal.Copy(ptr, argbValues, 0, bytes);

      //uint count = _FullScan(bmpData, argbValues);
      _OptimizedScan(bmpData, argbValues);

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
      //Font scaledFont = new Font(Font.FontFamily, (float)(scaleFactor * Font.GetHeight()), Font.Style, GraphicsUnit.Pixel);
      Font scaledFont = new Font(Font.FontFamily, (float)(scaleFactor * Font.Size), Font.Style, GraphicsUnit.Pixel);
      ObservedMetrics scaledMetrics = new(scaledFont);

      scaledMetrics.CharRect.Left = scaleFactor * CharRect.Left;
      scaledMetrics.CharRect.Right = scaleFactor * CharRect.Right;
      scaledMetrics.CharRect.Top = scaleFactor * CharRect.Top;
      scaledMetrics.CharRect.Bottom = scaleFactor * CharRect.Bottom;
      scaledMetrics.CharRect.Width = scaleFactor * CharRect.Width;
      scaledMetrics.CharRect.Height = scaleFactor * CharRect.Height;

      scaledMetrics.CellRect.Left = scaleFactor * CellRect.Left;
      scaledMetrics.CellRect.Right = scaleFactor * CellRect.Right;
      scaledMetrics.CellRect.Top = scaleFactor * CellRect.Top;
      scaledMetrics.CellRect.Bottom = scaleFactor * CellRect.Bottom;
      scaledMetrics.CellRect.Width = scaleFactor * CellRect.Width;
      scaledMetrics.CellRect.Height = scaleFactor * CellRect.Height;

      return scaledMetrics;
   }

   public double computeScaleFactor(int desiredCharHeight)
   {
      return desiredCharHeight / CharHeight;
   }
}
