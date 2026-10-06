//
// Plays an alert tone on the Hosyond 4" ESP32-S3 board's speaker.
//
// The speaker is driven by an ES8311 audio codec (configured over the I2C bus shared with
// the touch controller) and an FM8002E amplifier (enabled by IO1, active low). Audio
// data is streamed to the codec over I2S, with the codec clocked from the MCLK pin.
//
// Hardware: Hosyond 4" ESP32-S3 board with a speaker connected to the horn connector.
//

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>

#include <array>
#include <cmath>

#include "SerialX.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "NotificationMp3.h"

constexpr uint8_t AMP_ENABLE_PIN = 1;
constexpr uint8_t I2S_MCLK_PIN = 4;
constexpr uint8_t I2S_BCLK_PIN = 5;
constexpr uint8_t I2S_DOUT_PIN = 8;
constexpr uint8_t I2S_LRCK_PIN = 7;
constexpr uint8_t I2S_DIN_PIN = 6;
constexpr uint8_t I2C_SDA_PIN = 16;
constexpr uint8_t I2C_SCL_PIN = 15;
constexpr uint8_t ES8311_ADDRESS = 0x18;

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr float AMPLITUDE = 5000.0f;
constexpr float PLAYBACK_VOLUME = 0.25f;
constexpr uint8_t NUM_FRAME_SAMPLES = 64;
constexpr uint16_t FADE_MILLIS = 40;

constexpr float NOTE_C5 = 523.25f;
constexpr float NOTE_E5 = 659.25f;
constexpr float NOTE_G5 = 783.99f;
constexpr float NOTE_C6 = 1046.50f;

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

I2SClass i2s;

///
/// <summary>
/// Writes a single ES8311 codec register.
/// </summary>
/// <param name="reg">Register address</param>
/// <param name="value">Value to write</param>
///
void writeCodec(uint8_t reg, uint8_t value)
{
   Wire.beginTransmission(ES8311_ADDRESS);
   Wire.write(reg);
   Wire.write(value);
   const uint8_t error = Wire.endTransmission();
   if (error != 0)
   {
      Serial.printf("Codec write to reg 0x%02X failed, error %d\n", reg, error);
   }
}

///
/// <summary>
/// Configures the ES8311 codec for 16-bit I2S playback with a 256x MCLK, as a slave.
/// </summary>
///
void beginCodec()
{
   writeCodec(0x00, 0x1F);
   delay(20);
   writeCodec(0x00, 0x00);
   writeCodec(0x00, 0x80);

   writeCodec(0x44, 0x08);
   writeCodec(0x01, 0x30);
   writeCodec(0x02, 0x00);
   writeCodec(0x03, 0x10);
   writeCodec(0x16, 0x24);
   writeCodec(0x04, 0x10);
   writeCodec(0x05, 0x00);
   writeCodec(0x0B, 0x00);
   writeCodec(0x0C, 0x00);
   writeCodec(0x10, 0x1F);
   writeCodec(0x11, 0x7F);
   writeCodec(0x00, 0x80);

   writeCodec(0x01, 0x3F);
   writeCodec(0x06, 0x03);
   writeCodec(0x07, 0x00);
   writeCodec(0x08, 0xFF);
   writeCodec(0x09, 0x0C);
   writeCodec(0x0A, 0x0C);

   writeCodec(0x0D, 0x01);
   writeCodec(0x0E, 0x02);
   writeCodec(0x12, 0x00);
   writeCodec(0x13, 0x10);
   writeCodec(0x1C, 0x6A);
   writeCodec(0x37, 0x08);
   writeCodec(0x32, 0xBF);
   writeCodec(0x31, 0x00);
   writeCodec(0x14, 0x1A);
   writeCodec(0x17, 0xBF);
   writeCodec(0x15, 0x40);
   writeCodec(0x1B, 0x0A);
   writeCodec(0x45, 0x00);
}

///
/// <summary>
/// Streams a sine tone (or silence) to the speaker.
/// </summary>
/// <param name="frequency">Tone frequency in Hz; 0 for silence</param>
/// <param name="spanMillis">Duration in milliseconds</param>
///
void play(float frequency, uint16_t spanMillis)
{
   static float phase = 0;
   std::array<int16_t, NUM_FRAME_SAMPLES * 2> frame;
   const float phaseStep = 2.0f * PI * frequency / SAMPLE_RATE;
   const uint32_t numFrames = SAMPLE_RATE * spanMillis / 1000 / NUM_FRAME_SAMPLES;
   const float totalSamples = (float)numFrames * NUM_FRAME_SAMPLES;
   const float fadeSamples = SAMPLE_RATE * FADE_MILLIS / 1000.0f;

   for (uint32_t i = 0; i < numFrames; i++)
   {
      for (uint8_t j = 0; j < NUM_FRAME_SAMPLES; j++)
      {
         const float n = (float)i * NUM_FRAME_SAMPLES + j;
         const float envelope = min(1.0f, min(n / fadeSamples, (totalSamples - n) / fadeSamples));
         const int16_t sample = frequency > 0 ? (int16_t)(AMPLITUDE * envelope * sinf(phase)) : 0;
         frame[j * 2] = sample;
         frame[j * 2 + 1] = sample;
         phase += phaseStep;
         if (phase >= 2.0f * PI)
         {
            phase -= 2.0f * PI;
         }
      }

      i2s.write((uint8_t*)frame.data(), sizeof(frame));
   }
}

///
/// <summary>
/// Plays an in-memory WAV file. Supports 16-bit PCM, mono or stereo, at any sample rate
/// (resampled to the codec's rate using nearest-sample).
/// </summary>
/// <param name="wav">Pointer to the WAV file bytes</param>
/// <param name="size">Size of the WAV file in bytes</param>
/// <returns>True if the file was valid and played</returns>
///
bool playWav(const uint8_t* wav, size_t size)
{
   if (size < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0)
   {
      Serial.println("Not a WAV file");
      return false;
   }

   uint16_t format = 0;
   uint16_t numChannels = 0;
   uint32_t wavRate = 0;
   uint16_t bitsPerSample = 0;
   const uint8_t* data = nullptr;
   uint32_t dataSize = 0;

   size_t pos = 12;
   while (pos + 8 <= size)
   {
      uint32_t chunkSize;
      memcpy(&chunkSize, wav + pos + 4, 4);
      const uint8_t* chunk = wav + pos + 8;

      if (memcmp(wav + pos, "fmt ", 4) == 0)
      {
         memcpy(&format, chunk, 2);
         memcpy(&numChannels, chunk + 2, 2);
         memcpy(&wavRate, chunk + 4, 4);
         memcpy(&bitsPerSample, chunk + 14, 2);
      }
      else if (memcmp(wav + pos, "data", 4) == 0)
      {
         data = chunk;
         dataSize = min((size_t)chunkSize, size - pos - 8);
         break;
      }

      pos += 8 + chunkSize + (chunkSize & 1);
   }

   if (data == nullptr || format != 1 || bitsPerSample != 16 || numChannels < 1 || numChannels > 2 || wavRate == 0)
   {
      Serial.printf("Unsupported WAV: format %u, %u channels, %lu Hz, %u bits\n", format, numChannels, (unsigned long)wavRate, bitsPerSample);
      return false;
   }

   const int16_t* samples = (const int16_t*)data;
   const uint32_t numWavFrames = dataSize / (2 * numChannels);
   const uint32_t numOutFrames = (uint64_t)numWavFrames * SAMPLE_RATE / wavRate;
   std::array<int16_t, NUM_FRAME_SAMPLES * 2> frame;

   for (uint32_t i = 0; i < numOutFrames; i += NUM_FRAME_SAMPLES)
   {
      const uint32_t count = min((uint32_t)NUM_FRAME_SAMPLES, numOutFrames - i);
      for (uint32_t j = 0; j < count; j++)
      {
         const uint32_t src = (uint64_t)(i + j) * wavRate / SAMPLE_RATE;
         const int16_t left = samples[src * numChannels];
         const int16_t right = samples[src * numChannels + numChannels - 1];
         (int16_t)(left * PLAYBACK_VOLUME);
         (int16_t)(right * PLAYBACK_VOLUME);
      }

      i2s.write((uint8_t*)frame.data(), count * 2 * sizeof(int16_t));
   }

   return true;
}

///
/// <summary>
/// Plays an in-memory MP3 file (mono or stereo, any sample rate), resampled to the codec's
/// rate using nearest-sample.
/// </summary>
/// <param name="mp3">Pointer to the MP3 file bytes</param>
/// <param name="size">Size of the MP3 file in bytes</param>
///
void playMp3(const uint8_t* mp3, size_t size)
{
   static mp3dec_t decoder;
   static std::array<int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm;
   std::array<int16_t, NUM_FRAME_SAMPLES * 2> frame;
   mp3dec_frame_info_t info;
   uint8_t numOut = 0;
   float srcPos = 0;

   mp3dec_init(&decoder);

   while (size > 0)
   {
      const int numSamples = mp3dec_decode_frame(&decoder, mp3, size, pcm.data(), &info);
      if (info.frame_bytes == 0)
      {
         break;
      }
      mp3 += info.frame_bytes;
      size -= info.frame_bytes;

      if (numSamples == 0 || info.channels < 1)
      {
         continue;
      }

      const float step = (float)info.hz / SAMPLE_RATE;
      while (srcPos < numSamples)
      {
         const uint32_t src = (uint32_t)srcPos * info.channels;
         const int16_t left = pcm[src];
         const int16_t right = pcm[src + info.channels - 1];
         frame[numOut * 2] = (int16_t)(left * PLAYBACK_VOLUME);
         frame[numOut * 2 + 1] = (int16_t)(right * PLAYBACK_VOLUME);
         numOut++;
         srcPos += step;

         if (numOut == NUM_FRAME_SAMPLES)
         {
            i2s.write((uint8_t*)frame.data(), sizeof(frame));
            numOut = 0;
         }
      }
      srcPos -= numSamples;
   }

   if (numOut > 0)
   {
      i2s.write((uint8_t*)frame.data(), numOut * 2 * sizeof(int16_t));
   }
   Serial.printf("MP3 decoded: %d Hz, %d channels\n", info.hz, info.channels);
}

///
/// <summary>
/// Plays a gentle rising four-note chime.
/// </summary>
///
void alert()
{
   play(NOTE_C5, 200);
   play(NOTE_E5, 200);
   play(NOTE_G5, 200);
   play(NOTE_C6, 500);
   play(0, 100);
}

void setup()
{
   SerialX::begin();

   pinMode(AMP_ENABLE_PIN, OUTPUT);
   digitalWrite(AMP_ENABLE_PIN, LOW);

   Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

   beginCodec();

   i2s.setPins(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
   if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
   {
      Serial.println("I2S begin failed");
   }
}

void loop()
{
   Serial.println("Playing MP3");
   playMp3(NOTIFICATION_MP3, sizeof(NOTIFICATION_MP3));
   delay(2000);
}
