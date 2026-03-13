namespace SmoothFontCreator;

class Range
{
   private int _min = int.MaxValue;
   private int _max = 0;

   public void Update(int value)
   {
      _min = int.Min(_min, value);
      _max = int.Max(_max, value);
   }

   public override string ToString()
   {
      return _min == _max ? _min.ToString() : (_min + " " + _max);
   }
}
