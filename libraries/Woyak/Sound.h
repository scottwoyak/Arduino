#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <array>
#include <cmath>

#include <LovyanGFX.hpp>

#include "Logger.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

// By default every sound is compiled in. A sketch can define SOUNDS_CUSTOM plus SOUND_<NAME>
// (SOUND_CHIME, SOUND_ANNOUNCE, SOUND_MORNING, SOUND_WAR_HORN, SOUND_GJALLARHORN,
// SOUND_CHICKENS, SOUND_RAVEN, SOUND_NUCLEAR, SOUND_OBLITERATE, SOUND_DANGER,
// SOUND_BOND, SOUND_WHISTLE, SOUND_AMOK_TIME, SOUND_RED_ALERT, SOUND_BELLS_DONG,
// SOUND_CATHEDRAL, SOUND_ASIAN_GONG, SOUND_UNDERTAKER, SOUND_GONG_MUSIC, SOUND_SIREN,
// SOUND_SEWS, SOUND_LOTR_BATTLE, SOUND_JAWS, SOUND_HARRY_POTTER, SOUND_STAR_SPANGLED,
// SOUND_CAFE_BELL, SOUND_DOOR_BELL, SOUND_OLD_DOOR_BELL) before including this file to
// compile in only the sounds it uses, to save flash.
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_ANNOUNCE)
#include "Sounds/AnnounceMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHICKENS)
#include "Sounds/ChickensMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_GJALLARHORN)
#include "Sounds/GjallarhornMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_MORNING)
#include "Sounds/MorningRoosterMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CHIME)
#include "Sounds/NotificationMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WAR_HORN)
#include "Sounds/WarHornMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RAVEN)
#include "Sounds/RavenMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_NUCLEAR)
#include "Sounds/NuclearMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OBLITERATE)
#include "Sounds/ObliterateMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER)
#include "Sounds/DangerMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BOND)
#include "Sounds/BondMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WHISTLE)
#include "Sounds/WhistleMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_AMOK_TIME)
#include "Sounds/AmokTimeMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RED_ALERT)
#include "Sounds/RedAlertMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BELLS_DONG)
#include "Sounds/BellsDongMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CATHEDRAL)
#include "Sounds/CathedralMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_ASIAN_GONG)
#include "Sounds/AsianGongMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_UNDERTAKER)
#include "Sounds/UndertakerMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_GONG_MUSIC)
#include "Sounds/GongMusicMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_SIREN)
#include "Sounds/SirenMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_SEWS)
#include "Sounds/SewsMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_LOTR_BATTLE)
#include "Sounds/LotrBattleMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_JAWS)
#include "Sounds/JawsMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_HARRY_POTTER)
#include "Sounds/HarryPotterMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_STAR_SPANGLED)
#include "Sounds/StarSpangledMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CAFE_BELL)
#include "Sounds/CafeBellMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DOOR_BELL)
#include "Sounds/DoorBellMp3.h"
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OLD_DOOR_BELL)
#include "Sounds/OldDoorBellMp3.h"
#endif

// MP3 decoding needs more stack than the Arduino default. Any sketch using Sound must
// run its loop task with this larger stack.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

///
/// <summary>
/// Plays tones and in-memory MP3 files on boards with an ES8311 audio codec. Audio data
/// is streamed to the codec over I2S, with the codec clocked from the MCLK pin and
/// configured over I2C. All play methods block until the sound has finished unless the
/// async variants are used.
/// </summary>
///
class Sound
{
public:
   static constexpr int8_t DEFAULT_AMP_ENABLE_PIN = 1;
   static constexpr uint8_t DEFAULT_I2S_MCLK_PIN = 4;
   static constexpr uint8_t DEFAULT_I2S_BCLK_PIN = 5;
   static constexpr uint8_t DEFAULT_I2S_DOUT_PIN = 8;
   static constexpr uint8_t DEFAULT_I2S_LRCK_PIN = 7;
   static constexpr uint8_t DEFAULT_I2S_DIN_PIN = 6;
   static constexpr uint8_t DEFAULT_I2C_SDA_PIN = 16;
   static constexpr uint8_t DEFAULT_I2C_SCL_PIN = 15;

private:
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

   int8_t _ampEnablePin;
   uint8_t _mclkPin;
   uint8_t _bclkPin;
   uint8_t _doutPin;
   uint8_t _lrckPin;
   uint8_t _dinPin;
   uint8_t _sdaPin;
   uint8_t _sclPin;
   bool _useWire;
   I2SClass _i2s;
   mp3dec_t* _decoder = nullptr;
   int16_t* _pcm = nullptr;
   uint8_t* _mp3Cache = nullptr;
   const uint8_t* _mp3Source = nullptr;
   size_t _mp3CacheSize = 0;
   float _phase = 0;
   bool _began = false;
   volatile bool _playingAsync = false;
   volatile bool _stopRequested = false;
   volatile uint32_t _numWriteFailures = 0;
   uint8_t _asyncRepeats = 1;
   float _asyncMaxSecs = 0;
   TaskHandle_t _asyncTaskHandle = nullptr;
   StaticTask_t _asyncTaskBuffer;
   StackType_t* _asyncStack = nullptr;

   static void _asyncTask(void* param)
   {
      Sound* self = (Sound*)param;
      while (true)
      {
         ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
         self->_runAsync();
      }
   }

   void _runAsync()
   {
      Sound* self = this;
      if (self->_asyncMaxSecs > 0)
      {
         uint32_t maxMillis = (uint32_t)(self->_asyncMaxSecs * 1000.0f);
         uint32_t start = millis();
         uint32_t longest = 0;
         while (!self->_stopRequested)
         {
            uint32_t playStart = millis();
            self->playNotification();
            uint32_t now = millis();
            uint32_t duration = now - playStart;
            if (duration > longest)
            {
               longest = duration;
            }
            if (duration == 0 || (now - start) + longest > maxMillis)
            {
               break;
            }
         }
      }
      else
      {
         for (uint8_t i = 0; i < self->_asyncRepeats && !self->_stopRequested; i++)
         {
            self->playNotification();
         }
      }
      Serial.print("[Mem] t=");
      Serial.print(millis() / 1000);
      Serial.print("s SoundTask done, sound index ");
      Serial.print(self->soundIndex);
      Serial.print(": stack unused at minimum ");
      Serial.print(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t));
      Serial.print(" of ");
      Serial.print(ASYNC_STACK_BYTES);
      Serial.println(" bytes");
      self->_playingAsync = false;
   }

   ///
   /// <summary>
   /// Writes audio data to I2S, counting short writes so they can be logged later from a
   /// non-playback task (see _startAsync()).
   /// </summary>
   /// <param name="data">Bytes to write</param>
   /// <param name="size">Number of bytes</param>
   ///
   void _writeI2s(const uint8_t* data, size_t size)
   {
      if (_i2s.write(data, size) != size)
      {
         _numWriteFailures = _numWriteFailures + 1;
      }
   }

   ///
   /// <summary>
   /// Writes a single ES8311 codec register using the I2C driver that owns the bus.
   /// </summary>
   /// <param name="reg">Register address</param>
   /// <param name="value">Value to write</param>
   ///
   void _writeCodec(uint8_t reg, uint8_t value)
   {
      if (_useWire)
      {
         Wire.beginTransmission(ES8311_ADDRESS);
         Wire.write(reg);
         Wire.write(value);
         if (Wire.endTransmission(true) != 0)
         {
            Logger.log("Codec write to reg " + std::to_string(reg) + " failed", LogSeverity::ERROR);
         }
         return;
      }

      if (!lgfx::i2c::writeRegister8(I2C_PORT, ES8311_ADDRESS, reg, value, 0, I2C_FREQUENCY).has_value())
      {
         Logger.log("Codec write to reg " + std::to_string(reg) + " failed", LogSeverity::ERROR);
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
   static constexpr uint8_t NUM_SOUNDS = 28;

   /// Index of the notification sound to play (0 = Chime, 1 = Announce, 2 = Rooster, 3 = War Horn,
   /// 4 = Gjallarhorn, 5 = Chickens, 6 = Raven, 7 = Nuclear, 8 = Obliterate, 9 = Danger,
   /// 10 = Bond, 11 = Whistle, 12 = Amok Time, 13 = Red Alert, 14 = Bells Dong, 15 = Cathedral,
   /// 16 = Asian Gong, 17 = Undertaker, 18 = Gong Music, 19 = Siren, 20 = SEWS, 21 = LOTR Battle,
   /// 22 = Jaws, 23 = Harry Potter, 24 = Star Spangled, 25 = Cafe Bell, 26 = Door Bell,
   /// 27 = Old Door Bell).
   uint8_t soundIndex = 0;

   ///
   /// <summary>
   /// Initializes a new instance of the Sound class with the specified pin assignments.
   /// Defaults match the Hosyond 4" ESP32-S3 Viewer board.
   /// </summary>
   /// <param name="ampEnablePin">Active-low GPIO pin enabling the speaker amplifier, or -1 if none</param>
   /// <param name="mclkPin">I2S master clock pin</param>
   /// <param name="bclkPin">I2S bit clock pin</param>
   /// <param name="doutPin">I2S data-out pin (ESP32 to codec)</param>
   /// <param name="lrckPin">I2S word-select (LRCK) pin</param>
   /// <param name="dinPin">I2S data-in pin (codec to ESP32)</param>
   /// <param name="sdaPin">I2C data pin for the ES8311 codec</param>
   /// <param name="sclPin">I2C clock pin for the ES8311 codec</param>
   /// <param name="useWire">True to access the ES8311 codec via Arduino's Wire instead of lgfx::i2c</param>
   ///
   Sound(
      int8_t ampEnablePin = DEFAULT_AMP_ENABLE_PIN,
      uint8_t mclkPin = DEFAULT_I2S_MCLK_PIN,
      uint8_t bclkPin = DEFAULT_I2S_BCLK_PIN,
      uint8_t doutPin = DEFAULT_I2S_DOUT_PIN,
      uint8_t lrckPin = DEFAULT_I2S_LRCK_PIN,
      uint8_t dinPin = DEFAULT_I2S_DIN_PIN,
      uint8_t sdaPin = DEFAULT_I2C_SDA_PIN,
      uint8_t sclPin = DEFAULT_I2C_SCL_PIN,
      bool useWire = false)
      : _ampEnablePin(ampEnablePin),
        _mclkPin(mclkPin),
        _bclkPin(bclkPin),
        _doutPin(doutPin),
        _lrckPin(lrckPin),
        _dinPin(dinPin),
        _sdaPin(sdaPin),
        _sclPin(sclPin),
        _useWire(useWire)
   {
   }

   ///
   /// <summary>
   /// Gets the stable GUID of a notification sound, which remains constant even if the
   /// sound's display name or list position changes.
   /// </summary>
   /// <param name="index">Sound index</param>
   /// <returns>Sound GUID</returns>
   ///
   static const char* soundId(uint8_t index)
   {
      switch (index)
      {
      case 1:
         return "b7e4c9a1-3f28-4d62-9c15-8a0e7b54d201";
      case 2:
         return "e3a91f40-6b82-47d9-a52e-1c94f08d6302";
      case 3:
         return "4f8c2d17-9e05-43b8-8d61-7a35c20e9403";
      case 4:
         return "9a1d5e82-2c47-4f10-b389-6e40d71a8504";
      case 5:
         return "c6b30a49-8d14-42e7-95a2-3f18e62b7605";
      case 6:
         return "1e7f4b28-5a93-4c06-8e74-2b59a83c6706";
      case 7:
         return "7d2a9c63-1f58-48b1-a403-9c61b40d5807";
      case 8:
         return "f0c58e14-7b29-4e65-91d8-4a72c51e4908";
      case 9:
         return "3b9e6d51-4c80-41a2-87f5-5d83e92f3a09";
      case 10:
         return "8c4a1f76-0e32-49d4-b619-6f94a03b2b10";
      case 11:
         return "d5f13b98-6a71-45c0-92e4-7105b14c1c11";
      case 12:
         return "2a6c8e05-9f43-4b17-8c30-8216c25d0d12";
      case 13:
         return "6e0b4d29-3c15-47f8-a951-9327d36e9e13";
      case 14:
         return "b1d79a42-8e60-43c9-9407-a438e47f8f14";
      case 15:
         return "5c8e2f67-1b94-4a31-8f26-b549f5807015";
      case 16:
         return "a93f0c81-5d26-4e73-b148-c65a06916116";
      case 17:
         return "0d4b7e13-2f89-46a5-9c62-d76b17a25217";
      case 18:
         return "e71a5b36-9c02-4d84-8379-e87c28b34318";
      case 19:
         return "4b2d9c58-7e14-40f6-a590-f98d39c43419";
      case 20:
         return "9f6e1a70-3b48-48c2-9705-0a9e4ad52520";
      case 21:
         return "c38a4f92-0d51-4e19-8a23-1baf5be61621";
      case 22:
         return "1a5c7d04-6f83-42b0-b416-2cb06cf70722";
      case 23:
         return "7e9b2e18-4a37-49d5-9f38-3dc17d08f823";
      case 24:
         return "d04f8a25-1c69-45e1-8257-4ed28e19e924";
      case 25:
         return "3c1e6b49-8f20-4b74-a681-5fe39f2ada25";
      case 26:
         return "8a7d0f62-5e91-43c8-9042-60f4a03bcb26";
      case 27:
         return "f52b3c84-2a16-4f09-b893-7105b14cdc27";
      default:
         return "64c09e30-7d52-41a8-8514-09f8a73c1000";
      }
   }

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
         return "Nuclear";
      case 8:
         return "Obliterate";
      case 9:
         return "Danger";
      case 10:
         return "Bond";
      case 11:
         return "Whistle";
      case 12:
         return "Amok Time";
      case 13:
         return "Red Alert";
      case 14:
         return "Bells Dong";
      case 15:
         return "Cathedral";
      case 16:
         return "Asian Gong";
      case 17:
         return "Undertaker";
      case 18:
         return "Gong Music";
      case 19:
         return "Siren";
      case 20:
         return "SEWS";
      case 21:
         return "LOTR Battle";
      case 22:
         return "Jaws";
      case 23:
         return "Harry Potter";
      case 24:
         return "Star Spangled";
      case 25:
         return "Cafe Bell";
      case 26:
         return "Door Bell";
      case 27:
         return "Old Door Bell";
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
      if (_decoder == nullptr)
      {
         _decoder = (mp3dec_t*)heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM);
         _pcm = (int16_t*)heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM);
         if (_decoder == nullptr || _pcm == nullptr)
         {
            Logger.log("PSRAM allocation of MP3 decoder buffers failed", LogSeverity::ERROR);
            return;
         }
      }

      if (_ampEnablePin >= 0)
      {
         pinMode(_ampEnablePin, OUTPUT);
         digitalWrite(_ampEnablePin, LOW);
      }

      if (_useWire)
      {
         Wire.begin(_sdaPin, _sclPin, I2C_FREQUENCY);
      }
      else
      {
         lgfx::i2c::init(I2C_PORT, _sdaPin, _sclPin);
      }

      _beginCodec();

      _i2s.setPins(_bclkPin, _lrckPin, _doutPin, _dinPin, _mclkPin);
      _began = _i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
      if (!_began)
      {
         Logger.log("I2S begin failed", LogSeverity::ERROR);
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

         _writeI2s((uint8_t*)frame.data(), sizeof(frame));
      }
   }

   ///
   /// <summary>
   /// Plays an in-memory MP3
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

      if (_mp3Source != mp3)
      {
         if (_mp3Cache != nullptr)
         {
            heap_caps_free(_mp3Cache);
            _mp3Cache = nullptr;
            _mp3Source = nullptr;
         }
         _mp3Cache = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
         if (_mp3Cache == nullptr)
         {
            Logger.log("PSRAM allocation of " + std::to_string(size) + " bytes for sound failed", LogSeverity::ERROR);
            return;
         }
         memcpy(_mp3Cache, mp3, size);
         _mp3Source = mp3;
         _mp3CacheSize = size;
      }
      mp3 = _mp3Cache;

      mp3dec_init(_decoder);

      while (size > 0 && !_stopRequested)
      {
         const int numSamples = mp3dec_decode_frame(_decoder, mp3, size, _pcm, &info);
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
               _writeI2s((uint8_t*)frame.data(), sizeof(frame));
               numOut = 0;
            }
         }
         srcPos -= numSamples;
      }

      if (numOut > 0)
      {
         _writeI2s((uint8_t*)frame.data(), numOut * 2 * sizeof(int16_t));
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
         _writeI2s((uint8_t*)frame.data(), count * 2 * sizeof(int16_t));
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
         playMp3(CHICKENS_MP3, sizeof(CHICKENS_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RAVEN)
      case 6:
         playMp3(RAVEN_MP3, sizeof(RAVEN_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_NUCLEAR)
      case 7:
         playMp3(NUCLEAR_MP3, sizeof(NUCLEAR_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OBLITERATE)
      case 8:
         playMp3(OBLITERATE_MP3, sizeof(OBLITERATE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DANGER)
      case 9:
         playMp3(DANGER_MP3, sizeof(DANGER_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BOND)
      case 10:
         playMp3(BOND_MP3, sizeof(BOND_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_WHISTLE)
      case 11:
         playMp3(WHISTLE_MP3, sizeof(WHISTLE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_AMOK_TIME)
      case 12:
         playMp3(AMOK_TIME_MP3, sizeof(AMOK_TIME_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_RED_ALERT)
      case 13:
         playMp3(RED_ALERT_MP3, sizeof(RED_ALERT_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_BELLS_DONG)
      case 14:
         playMp3(BELLS_DONG_MP3, sizeof(BELLS_DONG_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CATHEDRAL)
      case 15:
         playMp3(CATHEDRAL_MP3, sizeof(CATHEDRAL_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_ASIAN_GONG)
      case 16:
         playMp3(ASIAN_GONG_MP3, sizeof(ASIAN_GONG_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_UNDERTAKER)
      case 17:
         playMp3(UNDERTAKER_MP3, sizeof(UNDERTAKER_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_GONG_MUSIC)
      case 18:
         playMp3(GONG_MUSIC_MP3, sizeof(GONG_MUSIC_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_SIREN)
      case 19:
         playMp3(SIREN_MP3, sizeof(SIREN_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_SEWS)
      case 20:
         playMp3(SEWS_MP3, sizeof(SEWS_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_LOTR_BATTLE)
      case 21:
         playMp3(LOTR_BATTLE_MP3, sizeof(LOTR_BATTLE_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_JAWS)
      case 22:
         playMp3(JAWS_MP3, sizeof(JAWS_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_HARRY_POTTER)
      case 23:
         playMp3(HARRY_POTTER_MP3, sizeof(HARRY_POTTER_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_STAR_SPANGLED)
      case 24:
         playMp3(STAR_SPANGLED_MP3, sizeof(STAR_SPANGLED_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_CAFE_BELL)
      case 25:
         playMp3(CAFE_BELL_MP3, sizeof(CAFE_BELL_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_DOOR_BELL)
      case 26:
         playMp3(DOOR_BELL_MP3, sizeof(DOOR_BELL_MP3));
         break;
#endif
#if !defined(SOUNDS_CUSTOM) || defined(SOUND_OLD_DOOR_BELL)
      case 27:
         playMp3(OLD_DOOR_BELL_MP3, sizeof(OLD_DOOR_BELL_MP3));
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
      _startAsync(repeats, 0);
   }

   ///
   /// <summary>
   /// Plays the selected notification on a background task, repeating it as long as another
   /// complete play fits within the time limit (capped at 6 seconds).
   /// </summary>
   /// <param name="maxSecs">Maximum total time in seconds</param>
   ///
   void playNotificationAsyncFor(float maxSecs)
   {
      _startAsync(1, maxSecs > MAX_REPEAT_SECS ? MAX_REPEAT_SECS : maxSecs);
   }

   static constexpr float MAX_REPEAT_SECS = 6.0f;

private:
   void _startAsync(uint8_t repeats, float maxSecs)
   {
      if (!_began)
      {
         Logger.log("Can't play sound: I2S not started", LogSeverity::ERROR);
         return;
      }

      if (_numWriteFailures > 0)
      {
         Logger.log(std::to_string(_numWriteFailures) + " I2S writes failed or were short since last sound", LogSeverity::ERROR);
         _numWriteFailures = 0;
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
      _asyncMaxSecs = maxSecs;

      if (_asyncTaskHandle == nullptr)
      {
         _asyncStack = (StackType_t*)heap_caps_malloc(ASYNC_STACK_BYTES, MALLOC_CAP_SPIRAM);
         if (_asyncStack != nullptr)
         {
            _asyncTaskHandle = xTaskCreateStaticPinnedToCore(
               _asyncTask,
               "SoundTask",
               ASYNC_STACK_BYTES / sizeof(StackType_t),
               this,
               1,
               _asyncStack,
               &_asyncTaskBuffer,
               0);
         }
         if (_asyncTaskHandle == nullptr)
         {
            Logger.log(
               "SoundTask creation failed: PSRAM stack of " + std::to_string(ASYNC_STACK_BYTES) + " bytes unavailable",
               LogSeverity::ERROR);
            return;
         }
      }

      _playingAsync = true;
      Serial.print("[Mem] t=");
      Serial.print(millis() / 1000);
      Serial.print("s SoundTask notify, sound index ");
      Serial.print(soundIndex);
      Serial.print(": free ");
      Serial.print(ESP.getFreeHeap());
      Serial.print(", largest ");
      Serial.println(ESP.getMaxAllocHeap());
      xTaskNotifyGive(_asyncTaskHandle);
   }

public:
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
