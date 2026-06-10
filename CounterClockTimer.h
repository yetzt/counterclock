#pragma once

#include <Arduino.h>

// A non-blocking countdown timer driven by repeated update calls.
class CounterClockTimer {
 public:
  // Create a stopped timer initialized to its full duration.
  explicit CounterClockTimer(uint32_t durationMs)
      : durationMs_(durationMs), remainingMs_(durationMs) {}

  // Subtract elapsed milliseconds and flag displayed-second changes.
  void update() {
    ticked_ = false;
    if (!running_) return;

    const uint32_t now = millis();
    const uint32_t delta = now - updatedAt_;
    updatedAt_ = now;

    if (delta >= remainingMs_) {
      remainingMs_ = 0;
      running_ = false;
    } else {
      remainingMs_ -= delta;
    }

    const uint32_t second = remainingSecondsCeil();
    ticked_ = second != previousSecond_;
    previousSecond_ = second;
  }

  // Resume counting down from the current remaining time.
  void start() {
    if (remainingMs_ == 0) return;
    updatedAt_ = millis();
    previousSecond_ = remainingSecondsCeil();
    running_ = true;
  }

  // Capture elapsed time and stop without resetting.
  void pause() {
    update();
    running_ = false;
  }

  // Restore the full configured duration and stop.
  void reset() {
    running_ = false;
    remainingMs_ = durationMs_;
    previousSecond_ = remainingSecondsCeil();
    ticked_ = true;
  }

  // Immediately complete and stop the timer.
  void finish() {
    remainingMs_ = 0;
    running_ = false;
    ticked_ = true;
  }

  // Replace the configured duration and reset the timer.
  void setDuration(uint32_t durationMs) {
    durationMs_ = durationMs;
    reset();
  }

  // Set a clamped remaining time while preserving the running state.
  void setRemaining(uint32_t remainingMs) {
    remainingMs_ = remainingMs > durationMs_ ? durationMs_ : remainingMs;
    updatedAt_ = millis();
    previousSecond_ = remainingSecondsCeil();
    ticked_ = true;
  }

  // Add or subtract time without leaving the valid duration range.
  void adjustRemaining(int32_t deltaMs) {
    int64_t value = static_cast<int64_t>(remainingMs_) + deltaMs;
    if (value < 0) value = 0;
    if (value > durationMs_) value = durationMs_;
    setRemaining(static_cast<uint32_t>(value));
  }

  // Report a newly reached ceiling-rounded second.
  bool justReachedSecond(uint32_t second) const {
    return ticked_ && remainingSecondsCeil() == second;
  }

  // Expose timer state and derived elapsed or remaining values.
  bool running() const { return running_; }
  bool paused() const { return !running_; }
  bool done() const { return remainingMs_ == 0; }
  bool ticked() const { return ticked_; }
  uint32_t remainingMs() const { return remainingMs_; }
  uint32_t elapsedMs() const { return durationMs_ - remainingMs_; }
  uint32_t elapsedSeconds() const { return elapsedMs() / 1000; }
  uint32_t remainingSecondsCeil() const { return (remainingMs_ + 999) / 1000; }

 private:
  // Internal countdown state updated from millis().
  uint32_t durationMs_;
  uint32_t remainingMs_;
  uint32_t updatedAt_ = 0;
  uint32_t previousSecond_ = 0;
  bool running_ = false;
  bool ticked_ = false;
};
