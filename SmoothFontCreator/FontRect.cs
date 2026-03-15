
namespace SmoothFontCreator;

public class FontRect
{
   public double Left = double.MaxValue;
   public double Right = 0;
   public double Top = double.MaxValue;
   public double Bottom = 0;
   public double Width => Right - Left + 1;
   public double Height => Bottom - Top + 1;

   public double AspectRatio => ((float)Width) / Height;

   public void Update(int x, int y)
   {
      Left = double.Min(Left, x);
      Right = double.Max(Right, x);
      Top = double.Min(Top, y);
      Bottom = double.Max(Bottom, y);
   }

   public void Intersect(FontRect other)
   {
      Left = double.Min(Left, other.Left);
      Right = double.Max(Right, other.Right);
      Top = double.Min(Top, other.Top);
      Bottom = double.Max(Bottom, other.Bottom);
   }

   public override string ToString()
   {
      return $"W:{Width}, H:{Height}, L:{Left}, R:{Right}, T:{Top}, B:{Bottom}";
   }
}
