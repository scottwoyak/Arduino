#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

#include <array>
#include <cmath>

#include <LovyanGFX.hpp>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "NotificationMp3.h"

// MP3 decoding needs more stack than the Arduino default. Any sketch using Sound must
// run its loop task with this larger stack.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

///
/// <summary>
/// Plays tones and in-memory MP3 files on the Hosyond 4" ESP32-S3 board's speaker. The
/// speaker is driven by an ES8311 audio codec (configured over the I2C bus shared with
/// the touch controller) and an FM8002E amplifier (enabled by IO1, active low). Audio
/// data is streamed to the codec over I2S, with the codec clocked from the MCLK pin.
/// All play methods block until the sound has finished.
/// </summary>
///
class Sound
{
private:
   static constexpr uint8_t AMP_ENABLE_PIN = 1;
   static constexpr uint8_t I2S_MCLK_PIN = 4;
   static constexpr uint8_t I2S_BCLK_PIN = 5;
   static constexpr uint8_t I2S_DOUT_PIN = 8;
   static constexpr uint8_t I2S_LRCK_PIN = 7;
   static constexpr uint8_t I2S_DIN_PIN = 6;
   static constexpr uint8_t I2C_SDA_PIN = 16;
   static constexpr uint8_t I2C_SCL_PIN = 15;
   static constexpr uint8_t I2C_PORT = 0;
   static constexpr uint32_t I2C_FREQUENCY = 400000;
   static constexpr uint8_t ES8311_ADDRESS = 0x18;

   static constexpr uint32_t SAMPLE_RATE = 16000;
   static constexpr float AMPLITUDE = 5000.0f;
   static constexpr uint8_t NUM_FRAME_SAMPLES = 64;
   static constexpr uint16_t FADE_MILLIS = 40;
   static constexpr uint32_t ASYNC_STACK_BYTES = 32768;

   static constexpr float NOTE_C5 = 523.25f;
   static constexpr float NOTE_E5 = 659.25f;
   static constexpr float NOTE_G5 = 783.99f;
   static constexpr float NOTE_C6 = 1046.50f;

   I2SClass _i2s;
   mp3dec_t _decoder;
   std::array<int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> _pcm;
   float _phase = 0;
   bool _began = false;
   volatile bool _playingAsync = false;
   uint8_t _asyncRepeats = 1;

   static void _asyncTask(void* param)
   {
      Sound* self = (Sound*)param;
      for (uint8_t i = 0; i < self->_asyncRepeats; i++)
      {
         self->playNotification();
      }
      self->_playingAsync = false;
      vTaskDelete(nullptr);
   }

   ///
   /// <summary>
   /// Writes a single ES8311 codec register. Uses the same I2C driver as the touch
   /// controller (which owns the bus), so the two don't conflict.
   /// </summary>
   /// <param name="reg">Register address</param>
   /// <param name="value">Value to write</param>
   ///
   void _writeCodec(uint8_t reg, uint8_t value)
   {
      if (!lgfx::i2c::writeRegister8(I2C_PORT, ES8311_ADDRESS, reg, value, 0, I2C_FREQUENCY).has_value())
      {
         Serial.printf("Codec write to reg 0x%02X failed\n", reg);
      }
   }

   ///
   /// <summary>
   /// Configures the ES8311 codec for 16-bit I2S playback with a 256x MCLK, as a slave.
   /// </summary>
   ///
   void _beginCodec()
   {
      _writeCodec(0x00, 0x1F);
      delay(20);
      _writeCodec(0x00, 0x00);
      _writeCodec(0x00, 0x80);

      _writeCodec(0x44, 0x08);
      _writeCodec(0x01, 0x30);
      _writeCodec(0x02, 0x00);
      _writeCodec(0x03, 0x10);
      _writeCodec(0x16, 0x24);
      _writeCodec(0x04, 0x10);
      _writeCodec(0x05, 0x00);
      _writeCodec(0x0B, 0x00);
      _writeCodec(0x0C, 0x00);
      _writeCodec(0x10, 0x1F);
      _writeCodec(0x11, 0x7F);
      _writeCodec(0x00, 0x80);

      _writeCodec(0x01, 0x3F);
      _writeCodec(0x06, 0x03);
      _writeCodec(0x07, 0x00);
      _writeCodec(0x08, 0xFF);
      _writeCodec(0x09, 0x0C);
      _writeCodec(0x0A, 0x0C);

      _writeCodec(0x0D, 0x01);
      _writeCodec(0x0E, 0x02);
      _writeCodec(0x12, 0x00);
      _writeCodec(0x13, 0x10);
      _writeCodec(0x1C, 0x6A);
      _writeCodec(0x37, 0x08);
      _writeCodec(0x32, 0xCF);
      _writeCodec(0x31, 0x00);
      _writeCodec(0x14, 0x1A);
      _writeCodec(0x17, 0xBF);
      _writeCodec(0x15, 0x40);
      _writeCodec(0x1B, 0x0A);
      _writeCodec(0x45, 0x00);
   }

public:
   /// Playback volume scale (1.0 = unchanged; values above 1.0 amplify and clip).
   float volume = 0.25f;

   ///
   /// <summary>
   /// Enables the amplifier, configures the codec, and starts the I2S stream. Call once
   /// after the display/touch controller has initialized the I2C bus.
   /// </summary>
   ///
   void begin()
   {
      pinMode(AMP_ENABLE_PIN, OUTPUT);
      digitalWrite(AMP_ENABLE_PIN, LOW);

      lgfx::i2c::init(I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);

      _beginCodec();

      _i2s.setPins(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
      _began = _i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
      if (!_began)
      {
         Serial.println("I2S begin failed");
      }
   }

   ///
   /// <summary>
   /// Streams a sine tone (or silence) to the speaker.
   /// </summary>
   /// <param name="frequency">Tone frequency in Hz; 0 for silence</param>
   /// <param name="spanMillis">Duration in milliseconds</param>
   ///
   void playTone(float frequency, uint16_t spanMillis)
   {
      if (!_began)
      {
         return;
      }

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
            const int16_t sample = frequency > 0 ? (int16_t)(AMPLITUDE * envelope * sinf(_phase)) : 0;
            frame[j * 2] = sample;
            frame[j * 2 + 1] = sample;
            _phase += phaseStep;
            if (_phase >= 2.0f * PI)
            {
               _phase -= 2.0f * PI;
            }
         }

         _i2s.write((uint8_t*)frame.data(), sizeof(frame));
      }
   }

   ///
   /// <summary>
   /// Plays an in-memory MP3 file (mono or stereo, any sample rate), resampled to the
   /// codec's rate using nearest-sample.
   /// </summary>
   /// <param name="mp3">Pointer to the MP3 file bytes</param>
   /// <param name="size">Size of the MP3 file in bytes</param>
   ///
   void playMp3(const uint8_t* mp3, size_t size)
   {
      if (!_began)
      {
         return;
      }

      std::array<int16_t, NUM_FRAME_SAMPLES * 2> frame;
      mp3dec_frame_info_t info;
      uint8_t numOut = 0;
      float srcPos = 0;

      mp3dec_init(&_decoder);

      while (size > 0)
      {
         const int numSamples = mp3dec_decode_frame(&_decoder, mp3, size, _pcm.data(), &info);
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
            const int16_t left = _pcm[src];
            const int16_t right = _pcm[src + info.channels - 1];
            frame[numOut * 2] = (int16_t)constrain((int32_t)(left * volume), -32768, 32767);
            frame[numOut * 2 + 1] = (int16_t)constrain((int32_t)(right * volume), -32768, 32767);
            numOut++;
            srcPos += step;

            if (numOut == NUM_FRAME_SAMPLES)
            {
               _i2s.write((uint8_t*)frame.data(), sizeof(frame));
               numOut = 0;
            }
         }
         srcPos -= numSamples;
      }

      if (numOut > 0)
      {
         _i2s.write((uint8_t*)frame.data(), numOut * 2 * sizeof(int16_t));
      }
   }

   ///
   /// <summary>
   /// Plays the built-in notification sound.
   /// </summary>
   ///
   void playNotification()
   {
      playMp3(NOTIFICATION_MP3, sizeof(NOTIFICATION_MP3));
   }

   ///
   /// <summary>
   /// Plays the built-in notification sound on a background task and returns immediately.
   /// Ignored if a background notification is already playing. Don't call other play
   /// methods while a background sound is playing.
   /// </summary>
   /// <param name="repeats">Number of times to play the notification</param>
   ///
   void playNotificationAsync(uint8_t repeats = 1)
   {
      if (!_began || _playingAsync)
      {
         return;
      }

      _asyncRepeats = repeats;
      _playingAsync = true;
      if (xTaskCreatePinnedToCore(_asyncTask, "SoundTask", ASYNC_STACK_BYTES, this, 1, nullptr, 0) != pdPASS)
      {
         _playingAsync = false;
      }
   }

   ///
   /// <summary>
   /// Plays a gentle rising four-note chime.
   /// </summary>
   ///
   void playAlert()
   {
      playTone(NOTE_C5, 200);
      playTone(NOTE_E5, 200);
      playTone(NOTE_G5, 200);
      playTone(NOTE_C6, 500);
      playTone(0, 100);
   }
};
