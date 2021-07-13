#pragma once

#include <NTPClient.h>

//
// Tracks when it is time to upload feed data back to the cloud
//
class FeedTimer {
private:
  unsigned long _millisAtNextSave;
  unsigned long _intervalSecs;
  bool _first = true;

public:
  //
  // @param intervalSecs the time between uploads, e.g. upload every 10 secs.
  // Uploads will occur at times even with a real clock, e.g. if you request
  // every minute, it will happen on the minute, not some random second value
  // @param uploadImmediately if true, a value will be uploaded right now and
  // the next occurs at the appropriate time
  //
  FeedTimer(unsigned long intervalSecs, bool uploadImmediately = true) {
    this->_intervalSecs = intervalSecs;
    this->_first = uploadImmediately;
  }

  //
  // Sets things up
  //
  void begin(NTPClient *clock) {

    //
    // figure out the next save event time such that it is
    // an even multiple, e.g. if we save every 10 minutes,
    // we're saving at 9:00, 9:10, etc...
    //

    // get the current time of day in total seconds
    int hours = clock->getHours();
    int mins = clock->getMinutes();
    int secs = clock->getSeconds();
    float currentSecs = 60 * 60 * hours + 60 * mins + secs;

    // add in a fudge factor for how long it takes our arduino to get
    // the time from the timeserver
    currentSecs += 0.5;

    // figure out the next round time value... the first one
    // greater than the current time
    long desiredSecs = 0;
    while (desiredSecs <= currentSecs) {
      desiredSecs += this->_intervalSecs;
    }

    // figure out the millis when we need to save again
    long deltaMillis = 1000 * (desiredSecs - currentSecs);
    this->_millisAtNextSave = millis() + deltaMillis;
  }

  //
  // Returns true when it is time to do an upload. Thereafter it will not
  // return true again until the interval amount of time has passed
  //
  bool ready() {
    if (this->_first) {
      this->_first = false;
      return true;
    } else {
      if (millis() > this->_millisAtNextSave) {
        // if there was a long delay (e.g. internet was out), keep
        // advancing until the next update is in the future
        while (millis() > this->_millisAtNextSave) {
          this->_millisAtNextSave += 1000*this->_intervalSecs;
        }
        return true;
      } else {
        return false;
      }
    }
  }

  unsigned long msUntilNextSave() {
    unsigned long now = millis();
    if (this->_millisAtNextSave > now) {
      return this->_millisAtNextSave - now;
    } else {
      return 0;
    }
  }
};
