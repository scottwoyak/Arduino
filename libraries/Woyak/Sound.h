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

// By default every sound is compiled in. A sketch can define SOUNDS_CUSTOM plus SOUND_<NAME>
// (SOUND_CHIME, SOUND_ANNOUNCE, SOUND_MORNING, SOUND_WAR_HORN, SOUND_GJALLARHORN,
// SOUND_CHICKENS, SOUND_RAVEN, SOUND_DANGER, SOUND_NUCLEAR, SOUND_OBLITERATE, SOUND_DANGER_2,
// SOUND_BOND, SOUND_WHISTLE, SOUND_AMOK_TIME, SOUND_RED_ALERT) before including this file to
// compile in only the sounds it uses, to save flash.
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_ANNOUNCE)
#include "AnnounceMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHICKENS)
#include "Chickens2Mp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_GJALLARHORN)
#include "GjallarhornMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_MORNING)
#include "MorningRoosterMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHIME)
#include "NotificationMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WAR_HORN)
#include "WarHornMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RAVEN)
#include "RavenMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER)
#include "DangerMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_NUCLEAR)
#include "NuclearMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OBLITERATE)
#include "ObliterateMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER_2)
#include "Danger2Mp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BOND)
#include "BondMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WHISTLE)
#include "WhistleMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_AMOK_TIME)
#include "AmokTimeMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RED_ALERT)
#include "RedAlertMp3.h"
#endif

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
   static constexpr uint8_t NUM_FRAME_SAMPLES = 64;
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
   volatile bool _stopRequested = false;
   uint8_t _asyncRepeats = 1;

   static void _asyncTask(void* param)
   {
      Sound* self = (Sound*)param;
      for (uint8_t i = 0; i < self->_asyncRepeats && !self->_stopRequested; i++)
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
      _writeCodec(0x32, 0xC5);
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

   /// Peak amplitude of generated tones (0 - 32767).
   float toneAmplitude = 5000.0f;

   /// Fade in/out duration of generated tones in milliseconds.
   uint16_t fadeMillis = 40;

   /// Number of selectable notification sounds.
   static constexpr uint8_t NUM_SOUNDS = 15;

   /// Index of the notification sound to play (0 = Chime, 1 = Announce, 2 = Rooster, 3 = War Horn,
   /// 4 = Gjallarhorn, 5 = Chickens, 6 = Raven, 7 = Danger, 8 = Nuclear, 9 = Obliterate,
   /// 10 = Danger 2, 11 = Bond, 12 = Whistle, 13 = Amok Time, 14 = Red Alert).
   uint8_t soundIndex = 0;

   ///
   /// <summary>
   /// Gets the display name of a notification sound.
   /// </summary>
   /// <param name="index">Sound index</param>
   /// <returns>Sound name</returns>
   ///
   static const char* soundName(uint8_t index)
   {
      switch (index)
      {
      case 1:
         return "Announce";
      case 2:
         return "Rooster";
      case 3:
         return "War Horn";
      case 4:
         return "Gjallarhorn";
      case 5:
         return "Chickens";
      case 6:
         return "Raven";
      case 7:
         return "Danger";
      case 8:
         return "Nuclear";
      case 9:
         return "Obliterate";
      case 10:
         return "Danger 2";
      case 11:
         return "Bond";
      case 12:
         return "Whistle";
      case 13:
         return "Amok Time";
      case 14:
         return "Red Alert";
      default:
         return "Chime";
      }
   }

   ///
   /// <summary>
   /// Sets the ES8311 codec's DAC volume register (0x00 = -95.5 dB, 0xBF = 0 dB, 0xFF = +32 dB,
   /// in 0.5 dB steps).
   /// </summary>
   /// <param name="value">Raw DAC volume register value</param>
   ///
   void setDacVolume(uint8_t value)
   {
      _writeCodec(0x32, value);
   }

   ///
   /// <summary>
   /// Enables the amplifier, configures the codec, and starts the I2S stream.
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
      const float fadeSamples = SAMPLE_RATE * fadeMillis / 1000.0f;

      for (uint32_t i = 0; i < numFrames; i++)
      {
         for (uint8_t j = 0; j < NUM_FRAME_SAMPLES; j++)
         {
            const float n = (float)i * NUM_FRAME_SAMPLES + j;
            const float envelope = min(1.0f, min(n / fadeSamples, (totalSamples - n) / fadeSamples));
            const int16_t sample = frequency > 0 ? (int16_t)(toneAmplitude * envelope * sinf(_phase)) : 0;
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

      while (size > 0 && !_stopRequested)
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
   /// Plays in-memory 16 kHz mono 16-bit PCM audio, scaled by the volume.
   /// </summary>
   /// <param name="pcm">Pointer to the samples</param>
   /// <param name="numSamples">Number of samples</param>
   ///
   void playPcm(const int16_t* pcm, size_t numSamples)
   {
      if (!_began)
      {
         return;
      }

      std::array<int16_t, NUM_FRAME_SAMPLES * 2> frame;
      size_t pos = 0;
      while (pos < numSamples && !_stopRequested)
      {
         const size_t count = min((size_t)NUM_FRAME_SAMPLES, numSamples - pos);
         for (size_t j = 0; j < count; j++)
         {
            const int16_t sample = (int16_t)constrain((int32_t)(pcm[pos + j] * volume), -32768, 32767);
            frame[j * 2] = sample;
            frame[j * 2 + 1] = sample;
         }
         _i2s.write((uint8_t*)frame.data(), count * 2 * sizeof(int16_t));
         pos += count;
      }
   }

   ///
   /// <summary>
   /// Plays the currently selected notification sound.
   /// </summary>
   ///
   void playNotification()
   {
      switch (soundIndex)
      {
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_ANNOUNCE)
      case 1:
         playMp3(ANNOUNCE_MP3, sizeof(ANNOUNCE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_MORNING)
      case 2:
         playMp3(MORNING_ROOSTER_MP3, sizeof(MORNING_ROOSTER_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WAR_HORN)
      case 3:
         playMp3(WAR_HORN_MP3, sizeof(WAR_HORN_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_GJALLARHORN)
      case 4:
         playMp3(GJALLARHORN_MP3, sizeof(GJALLARHORN_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHICKENS)
      case 5:
         playMp3(CHICKENS_2_MP3, sizeof(CHICKENS_2_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RAVEN)
      case 6:
         playMp3(RAVEN_MP3, sizeof(RAVEN_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER)
      case 7:
         playMp3(DANGER_MP3, sizeof(DANGER_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_NUCLEAR)
      case 8:
         playMp3(NUCLEAR_MP3, sizeof(NUCLEAR_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OBLITERATE)
      case 9:
         playMp3(OBLITERATE_MP3, sizeof(OBLITERATE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER_2)
      case 10:
         playMp3(DANGER_2_MP3, sizeof(DANGER_2_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BOND)
      case 11:
         playMp3(BOND_MP3, sizeof(BOND_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WHISTLE)
      case 12:
         playMp3(WHISTLE_MP3, sizeof(WHISTLE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_AMOK_TIME)
      case 13:
         playMp3(AMOK_TIME_MP3, sizeof(AMOK_TIME_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RED_ALERT)
      case 14:
         playMp3(RED_ALERT_MP3, sizeof(RED_ALERT_MP3));
         break;
#endif
      default:
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHIME)
         playMp3(NOTIFICATION_MP3, sizeof(NOTIFICATION_MP3));
#endif
         break;
      }
   }

   ///
   /// <summary>
   /// Plays the currently selected notification sound on a background task and returns
   /// immediately. If a background sound is already playing, it is stopped and restarted.
   /// Don't call other play methods while a background sound is playing.
   /// </summary>
   /// <param name="repeats">Number of times to play the notification</param>
   ///
   void playNotificationAsync(uint8_t repeats = 1)
   {
      if (!_began)
      {
         return;
      }

      if (_playingAsync)
      {
         _stopRequested = true;
         while (_playingAsync)
         {
            delay(1);
         }
      }
      _stopRequested = false;

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
