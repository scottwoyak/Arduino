using System.Buffers.Binary;

namespace SmoothFontCreator;

public class ByteWriter
{
   List<byte> _data = [];

   public void Add(UInt32 value)
   {
      value = BinaryPrimitives.ReverseEndianness(value);
      _data.AddRange(BitConverter.GetBytes(value));
   }

   public void Add(Int32 value)
   {
      value = BinaryPrimitives.ReverseEndianness(value);
      _data.AddRange(BitConverter.GetBytes(value));
   }

   public void Add(byte value)
   {
      _data.Add(value);
   }

   public void Add(Bitmap bitmap)
   {
      for (int y = 0; y < bitmap.Height; y++)
      {
         for (int x = 0; x < bitmap.Width; x++)
         {
            Color c = bitmap.GetPixel(x, y);
            _data.Add(c.A);
         }
      }
   }

   public byte[] GetBytes()
   {
      return _data.ToArray();
   }
}
