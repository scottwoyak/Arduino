using System.Buffers.Binary;

namespace SmoothFontCreator;

public class ByteReader
{
   private int _index = 0;
   private byte[] _data;

   public long RemainingBytes => _data.Length - _index;

   public ByteReader(byte[] data)
   {
      _data = data;
   }

   public UInt32 UInt32()
   {
      UInt32 value = BitConverter.ToUInt32(_data, _index);
      value = BinaryPrimitives.ReverseEndianness(value);

      _index += 4;
      return value;
   }

   public Int32 Int32()
   {
      Int32 value = BitConverter.ToInt32(_data, _index);
      value = BinaryPrimitives.ReverseEndianness(value);

      _index += 4;
      return value;
   }

   public byte Byte()
   {
      byte value = _data[_index];
      _index++;
      return value;
   }

   public Bitmap Bitmap(int width, int height)
   {
      Bitmap bitmap = new(width, height);

      for (int y = 0; y < height; y++)
      {
         for (int x = 0; x < width; x++)
         {
            byte alpha = this.Byte();
            bitmap.SetPixel(x, y, Color.FromArgb(alpha, 255, 255, 255));
         }
      }

      return bitmap;
   }
}
