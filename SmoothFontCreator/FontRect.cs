
namespace SmoothFontCreator;

public class FontRect
{
   public int Left = int.MaxValue;
   public int Right = 0;
   public int Top = int.MaxValue;
   public int Bottom = 0;
   public int Width => Right - Left+1;
   public int Height => Bottom - Top+1;


   public void Update(int x, int y)
   {
      Left = int.Min(Left, x);
      Right = int.Max(Right, x);
      Top = int.Min(Top, y);
      Bottom = int.Max(Bottom, y);
   }

   public void Intersect(FontRect other)
   {
      Left = int.Min(Left, other.Left);
      Right = int.Max(Right, other.Right);
      Top = int.Min(Top, other.Top);
      Bottom = int.Max(Bottom, other.Bottom);
   }
}
