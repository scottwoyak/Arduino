using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

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
}

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
      for (int x = 0; x < bitmap.Width; x++)
      {
         for (int y = 0; y < bitmap.Height; y++)
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


public class VLWGlyph
{
   public Int32 uChar;
   public Int32 Height;
   public Int32 Width;
   public Int32 gxAdvance;
   public Int32 gdY;
   public Int32 gdX;
   public Int32 padding = 0;
   public Bitmap Bitmap;
}

public class VLWFont
{
   public UInt32 Version { get; internal set; } = 11;
   public UInt32 FontSizePx { get; internal set; }
   public Int32 Ascent { get; internal set; }
   public Int32 Descent { get; internal set; }

   public String FontName { get; internal set; } = "";
   public String PostscriptName { get; internal set; } = "";
   public bool AntiAliased { get; internal set; } = true;

   public Dictionary<Int32, VLWGlyph> Glyphs = [];

   private void _load(byte[] data)
   {
      ByteReader reader = new(data);
      uint gCount = reader.UInt32();
      Version = reader.UInt32();
      FontSizePx = reader.UInt32();
      reader.UInt32(); // deprecated mboxY value
      Ascent = reader.Int32();
      Descent = reader.Int32();

      for (int i = 0; i < gCount; i++)
      {
         VLWGlyph g = new();
         g.uChar = reader.Int32();
         g.Height = reader.Int32();
         g.Width = reader.Int32();
         g.gxAdvance = reader.Int32();
         g.gdY = reader.Int32();
         g.gdX = reader.Int32();
         g.padding = reader.Int32();
         Glyphs.Add(g.uChar, g);
      }

      foreach (VLWGlyph g in Glyphs.Values)
      {
         if (g.Width > 0 && g.Height > 0)
         {
            g.Bitmap = new Bitmap(g.Width, g.Height);

            for (int y = 0; y < g.Height; y++)
            {
               for (int x = 0; x < g.Width; x++)
               {
                  byte alpha = reader.Byte();
                  g.Bitmap.SetPixel(x, y, Color.FromArgb(alpha, 255, 255, 255));
                  //g.Bitmap.SetPixel(x, y, Color.FromArgb(255, alpha, alpha, alpha));
               }
            }
         }
      }

      if (reader.RemainingBytes > 0)
      {
         byte nameLength = reader.Byte();
         for (int i = 0; i < nameLength; i++)
         {
            FontName += (char)reader.Byte();
         }
         reader.Byte(); // \0 string termination

         byte postscriptNameLength = reader.Byte();
         for (int i = 0; i < postscriptNameLength; i++)
         {
            PostscriptName += (char)reader.Byte();
         }
         reader.Byte(); // \0 string termination

         AntiAliased = reader.Byte() > 0;
      }
   }

   public VLWFont()
   {

   }

   public VLWFont(byte[] data)
   {
      _load(data);
   }

   public VLWFont(string vlwFile)
   {
      _load(File.ReadAllBytes(vlwFile));
   }

   public byte[] ToByteArray()
   {
      ByteWriter writer = new();

      writer.Add(Glyphs.Count);
      writer.Add(Version);
      writer.Add(FontSizePx);
      writer.Add(0); // deprecated mboxY value
      writer.Add(Ascent);
      writer.Add(Descent);

      foreach (VLWGlyph g in Glyphs.Values)
      {
         writer.Add(g.uChar);
         writer.Add(g.Height);
         writer.Add(g.Width);
         writer.Add(g.gxAdvance);
         writer.Add(g.gdY);
         writer.Add(g.gdX);
         writer.Add(g.padding);
      }

      foreach (VLWGlyph g in Glyphs.Values)
      {
         if (g.Bitmap != null)
         {
            writer.Add(g.Bitmap);
         }
      }

      return writer.GetBytes();
   }

   public void SaveAsVLW()
   {

   }

   public void SaveAsSmoothFont(string filePath)
   {
      StringBuilder sb = new();

      sb.Append("#pragma once\n");
      sb.Append("\n");
      sb.Append("const uint8_t Scott[] PROGMEM = {\n");

      byte[] data = ToByteArray();
      for (int i = 0; i < data.Length; i++)
      {
         sb.Append(data[i].ToHex());
         sb.Append(", ");

         if ((i + 1) % 16 == 0)
         {
            sb.Append('\n');
         }
      }

      sb.Append('\n');
      sb.Append("};");

      File.WriteAllText(filePath, sb.ToString());
   }
}
