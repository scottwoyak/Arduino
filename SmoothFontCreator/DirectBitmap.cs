
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

namespace SmoothFontCreator;

public class DirectBitmap : IDisposable
{
   public Bitmap Bitmap { get; }
   public bool ReadOnly { get; }

   public int Width => _bmpData.Width;
   public int Height => _bmpData.Height;

   private BitmapData _bmpData;
   private byte[] _argbValues;

   public DirectBitmap(Bitmap bitmap, bool readOnly = false, Rectangle? rect = null)
   {
      Bitmap = bitmap;

      if (rect == null)
      {
         rect = new Rectangle(new Point(0, 0), bitmap.Size);
      }

      // lock upon creation
      ImageLockMode mode = readOnly ? ImageLockMode.ReadOnly : ImageLockMode.ReadWrite;
      _bmpData = bitmap.LockBits((Rectangle) rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);

      // Calculate total bytes; Stride handles row padding
      int bytes = Math.Abs(_bmpData.Stride) * _bmpData.Height;
      _argbValues = new byte[bytes];

      // read all the data
      Marshal.Copy(_bmpData.Scan0, _argbValues, 0, _argbValues.Length);
   }

   public void Dispose()
   {
      if (ReadOnly == false)
      {
         // copy data back to the bitmap
         Marshal.Copy(_argbValues, 0, _bmpData.Scan0, _argbValues.Length);
      }

      // unlock
      Bitmap.UnlockBits(_bmpData); // Mandatory
   }

   public Color GetPixel(int x, int y)
   {
      int i = y * _bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)

      byte a = _argbValues[i + 3];
      byte r = _argbValues[i + 2];
      byte g = _argbValues[i + 1];
      byte b = _argbValues[i + 0];

      return Color.FromArgb(a, r, g, b);
   }

   public void SetPixel(int x, int y, Color c)
   {
      int i = y * _bmpData.Stride + x * 4; // 4 bytes per pixel (ARGB)

      _argbValues[i + 3] = c.A;
      _argbValues[i + 2] = c.R;
      _argbValues[i + 1] = c.G;
      _argbValues[i + 0] = c.B;
   }


   public Color GetPixel(RectangleF rect)
   {
      double r = 0;
      double g = 0;
      double b = 0;
      double a = 0;

      int x1 = (int)Math.Floor(rect.X);
      int x2 = (int)Math.Floor(rect.Right);
      int y1 = (int)Math.Floor(rect.Y);
      int y2 = (int)Math.Floor(rect.Bottom);


      double xLeftFactor = 1;
      double xRightFactor = 1;
      double yTopFactor = 1;
      double yBottomFactor = 1;

      if (x1 < 0)
      {
         xLeftFactor = 1;
         x1 = 0;
      }
      else
      {
         xLeftFactor = 1 - (rect.X % 1);
      }

      if (y1 < 0)
      {
         yTopFactor = 1;
         y1 = 0;
      }
      else
      {
         yTopFactor = 1 - (rect.Y % 1);
      }

      if (x2 > Width - 1)
      {
         x2 = Width - 1;
         xRightFactor = 1;
      }
      else
      {
         xRightFactor = rect.Right % 1;
      }

      if (y2 > Height - 1)
      {
         y2 = Height - 1;
         yBottomFactor = 1;
      }
      else
      {
         yBottomFactor = rect.Bottom % 1;
      }

      double xFactor = 1;
      double yFactor = 1;
      for (int y = y1; y <= y2; y++)
      {
         if (y == y1)
         {
            yFactor = yTopFactor;
         }
         else if (y == y2)
         {
            yFactor = yBottomFactor;
         }
         else
         {
            yFactor = 1;
         }

         for (int x = x1; x <= x2; x++)
         {
            if (x == x1)
            {
               xFactor = xLeftFactor;
            }
            else if (x == x2)
            {
               xFactor = xRightFactor;
            }
            else
            {
               xFactor = 1;
            }

            Color c = GetPixel(x, y);
            r += xFactor * yFactor * c.R;
            g += xFactor * yFactor * c.G;
            b += xFactor * yFactor * c.B;
            a += xFactor * yFactor * c.A;
         }
      }

      //double area = Math.Ceiling(rect.Width) * Math.Ceiling(rect.Height);
      double area = rect.Width * rect.Height;
      return Color.FromArgb(
         (byte)Math.Min(255, (a / area)),
         (byte)Math.Min(255, (r / area)),
         (byte)Math.Min(255, (g / area)),
         (byte)Math.Min(255, (b / area))
         );
   }
}
