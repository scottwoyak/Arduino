namespace SmoothFontCreator;

class Metric
{
   public int xMin = int.MaxValue;
   public int xMax = 0;
   public int yMin = int.MaxValue;
   public int yMax = 0;
   public int Width => xMax - xMin;
   public int Height => yMax - yMin;

   public void update(int x, int y)
   {
      xMin = int.Min(xMin, x);
      xMax = int.Max(xMax, x);
      yMin = int.Min(yMin, y);
      yMax = int.Max(yMax, y);
   }
}
