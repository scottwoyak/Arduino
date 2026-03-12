using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics.Tracing;
using System.Reflection.Metadata.Ecma335;
using System.Text;

namespace SmoothFontCreator;

public class ByteReader
{
   private int _index = 0;
   private byte[] _data;

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


public class Glyph
{
   public Int32 uChar;
   public Int32 Height;
   public Int32 Width;
   public Int32 gxAdvance;
   public Int32 dY;
   public Int32 dX;
   public Int32 padding = 0;
   public Bitmap Bitmap;
}

public class VLWContent
{
   // header
   public UInt32 Version { get; internal set; }
   public UInt32 FontSizePx { get; internal set; }
   public UInt32 Ascent { get; internal set; }
   public UInt32 Descent { get; internal set; }

   List<Glyph> Glyphs = [];

   private void _load(byte[] data)
   {
      ByteReader reader = new(data);
      uint gCount = reader.UInt32();
      Version = reader.UInt32();
      FontSizePx = reader.UInt32();
      reader.UInt32(); // deprecated mboxY value
      Ascent = reader.UInt32();
      Descent = reader.UInt32();

      for (int i = 0; i < gCount; i++)
      {
         Glyph g = new();
         g.uChar = reader.Int32();
         g.Height = reader.Int32();
         g.Width = reader.Int32();
         g.gxAdvance = reader.Int32();
         g.dY = reader.Int32();
         g.dX = reader.Int32();
         g.padding = reader.Int32();
         Glyphs.Add(g);
      }

      for (int i = 0; i < gCount; i++)
      {
         Glyph g = Glyphs[i];

         if (g.Width > 0 && g.Height > 0)
         {
            g.Bitmap = new Bitmap(g.Width, g.Height);

            for (int x = 0; x < g.Width; x++)
            {
               for (int y = 0; y < g.Height; y++)
               {
                  byte alpha = reader.Byte();
                  g.Bitmap.SetPixel(x, y, Color.FromArgb(alpha, 255, 255, 255));
               }
            }
         }
      }
   }

   public VLWContent(byte[] data)
   {
      _load(data);
   }

   public VLWContent(string vlwFile)
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

      foreach (Glyph g in Glyphs)
      {
         writer.Add(g.uChar);
         writer.Add(g.Height);
         writer.Add(g.Width);
         writer.Add(g.gxAdvance);
         writer.Add(g.dY);
         writer.Add(g.dX);
         writer.Add(g.padding);
      }

      foreach (Glyph g in Glyphs)
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

   public void SaveAsSmoothFont()
   {

   }
}
