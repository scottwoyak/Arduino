#pragma once

class Stopwatch {
private:
  unsigned long _startMicros;
  unsigned long _elapsedMicros = 0;
  bool _running = false;

public:
  Stopwatch() { Start(); }

  void Start() {
    if (this->_running == false) {
      _running = true;
      _startMicros = micros();
    }
  }

  void Stop() {
    if (this->_running) {
      this->_elapsedMicros += (micros() - this->_startMicros);
      this->_running = false;
    }
  }

  void reset() {
    this->_elapsedMicros = 0;
    if (this->_running) {
      this->_startMicros = micros();
    }
  }

  unsigned long elapsedMicros() {
    if (this->_running) {
      return (micros() - this->_startMicros) + this->_elapsedMicros;
    } else {
      return this->_elapsedMicros;
    }
  }

  double elapsedMillis() { return elapsedMicros() / 1000.0; }
  double elapsedSecs() { return elapsedMicros() / 1000000.0; }

  void printlnMicros(const char *label, bool reset = false) {
    Serial.print(label);
    Serial.print(" ");
    Serial.print(this->elapsedMicros());
    Serial.println(" micros");

    if (reset) {
      this->reset();
    }
  }

  void printlnMillis(const char *label, bool reset = false) {
    Serial.print(label);
    Serial.print(" ");
    Serial.print(this->elapsedMillis());
    Serial.println("ms");

    if (reset) {
      this->reset();
    }
  }

  void printlnSecs(const char *label, bool reset = false) {
    Serial.print(label);
    Serial.print(" ");
    Serial.print(this->elapsedSecs());
    Serial.println("s");

    if (reset) {
      this->reset();
    }
  }
};
