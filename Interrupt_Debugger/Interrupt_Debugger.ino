
#include "Feather.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "Util.h"

Feather feather;

constexpr uint8_t PIN = A5;
constexpr uint8_t TRIGGER_PIN = 5;

enum Source
{
   Loop,
   Interrupt,
   Unknown,
};

class Item
{
public:
   unsigned long micros;
   int state;
   Source source;

   Item()
   {
      micros = 0;
      state = -1;
      source = Source::Unknown;
   }
};

constexpr uint16_t SIZE = 1000;

class InterruptHistory
{
private:
   volatile bool _stopped = false;
   volatile uint16_t _index = 0;
   volatile Item _items[SIZE];

public:
   void record(int state, Source source)
   {
      if (_stopped)
      {
         return;
      }

      uint16_t index = _index;
      if (index < SIZE)
      {
         _items[index].micros = micros();
         _items[index].state = state;
         _items[index].source = source;
         _index = _index + 1;
      }
   }

   void reset()
   {
      _index = 0;
      _stopped = false;
   }

   void printAll()
   {
      if (_index == 0)
      {
         return;
      }

      Serial.println("\n\tState\tSource\tDelta\tTotal");
      for (int16_t i = 1; i < _index; i++)
      {
         Serial.print(i + 1);
         Serial.print("\t");
         Serial.print(_items[i].state == HIGH ? "    -|" : "|-");
         Serial.print("\t");
         Serial.print(_items[i].source == Source::Loop ? "Loop" : "Inter");
         Serial.print("\t");
         if (i > 0)
         {
            Serial.print(Util::getSpan(_items[i - 1].micros, _items[i].micros));
         }
         Serial.print("\t");
         Serial.print(_items[i].micros);
         Serial.println();
      }

      delay(0); // so that Serial doesn't crash
   }

   void printInterrupts()
   {
      if (_index == 0)
      {
         return;
      }

      Serial.println("\n\tState\tSource\tDelta\tTotal");
      for (int16_t i = 0; i < _index; i++)
      {
         if (_items[i].source == Source::Interrupt)
         {
            Serial.print(i + 1);
            Serial.print("\t");
            Serial.print(_items[i].state == HIGH ? "    -|" : "|-");
            Serial.print("\t");
            Serial.print(_items[i].source == Source::Loop ? "Loop" : "Inter");
            Serial.print("\t");
            if (i > 0)
            {
               Serial.print(Util::getSpan(_items[i - 1].micros, _items[i].micros));
            }
            Serial.print("\t");
            Serial.print(_items[i].micros);

            if (i > 0 && _items[i].state == _items[i-1].state)
            {
               if (_items[i].micros - _items[i - 1].micros > 100)
               {
                  if (_items[i].state == HIGH)
                  {
                     Serial.print(" ^^^^^^^^^^^^^^^^^^^");
                  }
                  else
                  {
                     Serial.print(" -------------------");
                  }
               }
               else
               {
                  Serial.print(" *******************");
               }
            }
            Serial.println();
         }
      }

      delay(0); // so that Serial doesn't crash
   }

   void printFalseHighs()
   {
      _stopped = true;
      if (_index > 0)
      {
         bool errFound = false;
         for (int16_t i = 1; i < _index; i++)
         {
            if (_items[i].source == Source::Interrupt && _items[i - 1].state == HIGH)
            {
               // if an interrupt occurs while the state is HIGH, we have a false event
               if (Util::getSpan(_items[i - 1].micros, _items[i - 1].micros) > 20)
               {
                  errFound = true;
               }
            }

            if (errFound && _items[i].state == LOW)
            {
               errFound = false;

               // start of the event
               int16_t iEnd = i;
               int16_t iStart = i - 1;
               while (iStart >= 0 && _items[iStart].state == HIGH)
               {
                  iStart--;
               }

               Serial.println();

               for (int j = iStart; j <= iEnd; j++)
               {
                  Serial.print(j + 1);
                  Serial.print("\t");
                  Serial.print(_items[j].state == HIGH ? "     |" : "|");
                  Serial.print("\t");
                  Serial.print(_items[j].source == Source::Loop ? "Loop" : "Inter");
                  Serial.print("\t");
                  Serial.print(Util::getSpan(_items[iStart].micros, _items[j].micros));
                  if (j > iStart)
                  {
                     Serial.print("\t");
                     Serial.print(Util::getSpan(_items[j - 1].micros, _items[j].micros));
                  }
                  Serial.println();
               }
               Serial.println();
            }

            delay(0); // so that Serial doesn't crash
         }
      }
   }
};

InterruptHistory history;
Stopwatch sw;

unsigned long microsAtChange = 0;
int state;
void tickInterrupt()
{
   int newState = digitalRead(PIN);
   history.record(newState, Source::Interrupt);

   if (micros() - microsAtChange < 100)
   {
      return;
   }

   if (newState != state)
   {
      state = newState;
      microsAtChange = micros();
   }
   else
   {
      digitalWrite(TRIGGER_PIN, state);
   }
}

int lastState = LOW;
void setup()
{
   SerialX::begin();
   feather.begin();

   pinMode(BUILTIN_LED, OUTPUT);
   pinMode(TRIGGER_PIN, OUTPUT);
   digitalWrite(TRIGGER_PIN, HIGH);
   pinMode(PIN, INPUT_PULLUP);
   attachInterrupt(digitalPinToInterrupt(PIN), tickInterrupt, CHANGE);

   // provide time to let the above changes settle
   delay(100);
   digitalWrite(BUILTIN_LED, digitalRead(PIN));

   // reset all
   sw.reset();
   history.reset();
   lastState = digitalRead(PIN);
}

void loop()
{
   int state = digitalRead(PIN);
   if (state != lastState)
   {
      //noInterrupts();
      //history.record(state, Source::Loop);
      //interrupts();
      digitalWrite(BUILTIN_LED, state);
      lastState = state;
   }

   if (sw.elapsedSecs() > 2)
   {
      //history.printFalseHighs();
      history.printInterrupts();
      //history.printAll();
      history.reset();
      sw.reset();
   }
}

