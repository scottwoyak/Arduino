#pragma once

#include <deque>

#include <Arduino.h>

///
/// <summary>
/// A Stream implementation that mimics Serial's API but reads/writes over the
/// DeviceServerClient WebSocket connection instead of a UART. Output written to this
/// stream is sent to the DeviceServer as "shellOutput" messages; input arrives via
/// "shellInput" messages, which DeviceServerClient pushes into this stream with
/// _onInput(). This lets the same shell/command-processing code run unmodified
/// against either Serial or this stream, by writing it against Stream/Print instead
/// of the concrete Serial type.
/// </summary>
/// <remarks>
/// Output is sent one line at a time: write() buffers characters until a '\n' is
/// seen, then flushes the buffered text as a single "shellOutput" message via the
/// send callback given to begin(). Input is a simple FIFO queue of characters fed by
/// _onInput(); available()/read()/peek() consume it the way Serial would consume
/// incoming UART bytes.
/// </remarks>
///
class WebSocketStream : public Stream
{
public:
   ///
   /// <summary>
   /// Registers the callback used to actually send buffered output text to the
   /// DeviceServer. Must be called once before this stream is used for output.
   /// </summary>
   /// <param name="sendText">Function invoked with a line of output text to send, including its trailing newline.</param>
   ///
   void begin(void (*sendText)(const char* text))
   {
      _sendText = sendText;
   }

   ///
   /// <summary>
   /// Appends text received from a "shellInput" message to the input queue, available
   /// to subsequent available()/read()/peek() calls.
   /// </summary>
   /// <param name="text">Input text received from the DeviceServer (one line, no trailing newline).</param>
   ///
   void _onInput(const char* text)
   {
      for (const char* p = text; *p != '\0'; p++)
      {
         _inQueue.push_back(*p);
      }
      _inQueue.push_back('\n');
   }

   // ----------- Print (output)
   size_t write(uint8_t c) override
   {
      _outBuffer += (char)c;
      if (c == '\n')
      {
         _flush();
      }
      return 1;
   }

   size_t write(const uint8_t* buffer, size_t size) override
   {
      for (size_t i = 0; i < size; i++)
      {
         write(buffer[i]);
      }
      return size;
   }

   // ----------- Stream (input)
   int available() override
   {
      return (int)_inQueue.size();
   }

   int read() override
   {
      if (_inQueue.empty())
      {
         return -1;
      }

      char c = _inQueue.front();
      _inQueue.pop_front();
      return (unsigned char)c;
   }

   int peek() override
   {
      return _inQueue.empty() ? -1 : (unsigned char)_inQueue.front();
   }

   ///
   /// <summary>
   /// Sends any buffered output text immediately as a "shellOutput" message, even if it
   /// doesn't end with a newline. Needed after an inline prompt (e.g. print("Enter location: ")
   /// with no trailing newline, so the terminal cursor stays on the same line) since write()
   /// otherwise only flushes once a '\n' is seen.
   /// </summary>
   ///
   void flushOutput()
   {
      _flush();
   }

private:
   void (*_sendText)(const char* text) = nullptr;
   std::deque<char> _inQueue;
   std::string _outBuffer;

   ///
   /// <summary>
   /// Sends the buffered output text as one "shellOutput" message and clears the buffer.
   /// </summary>
   ///
   void _flush()
   {
      if (_sendText != nullptr)
      {
         _sendText(_outBuffer.c_str());
      }
      _outBuffer.clear();
   }
};
