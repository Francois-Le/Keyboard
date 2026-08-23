#pragma once
#include "config.h"
#include "event.h"
#include "pos.h"

/// What the main loop should do with the event currently at the front of the event queue.
enum class OverlapAction : uint8_t {
  /// Nothing to do for overlap removal, process the front event normally.
  PROCESS,

  /// We don't have enough information yet to decide. The main loop must stop processing events and
  /// poll the switches again. This is the only case where overlap removal adds latency.
  WAIT,

  /// The front key was released too late: emit it as a tap and drop its press/release pair, leaving
  /// the key that was pressed in between to be processed normally on the resulting layer.
  EMIT_TAP,
};

/// Result of scanning the event queue after the front event. Filled in by the caller, which is the
/// only side that knows how to walk the queue.
struct OverlapScan {
  /// Number of presses of a key other than the front one seen before the front key was released.
  uint8_t m_interveningPressCount = 0;

  /// Timestamp of the first of those presses. Only meaningful if m_interveningPressCount > 0.
  unsigned long m_firstInterveningPressTime = 0;

  /// True if the release of the front key was found in the queue.
  bool m_foundRelease = false;

  /// Timestamp of that release. Only meaningful if m_foundRelease is true.
  unsigned long m_releaseTime = 0;

  /// Position of that release in the queue. Only meaningful if m_foundRelease is true.
  EventQueue::Iterator m_releaseIt{ 0 };
};

/// Decide what to do with the front event of the queue with regard to overlap removal.
///
/// Overlap removal fixes the case where a key that is both a "tap" key and a layer key is released
/// slightly too late. Typing " a" often reports:
///     press SPACE, press A, release SPACE, release A
/// The "on release" code sees the press of A before the release of SPACE, so it declines the tap,
/// activates the accent layer, and A comes out as 'a' with an accent. What the user meant was a tap
/// of SPACE followed by A: they simply released SPACE a few milliseconds late.
///
/// We detect that race and emit the tap ourselves, so the layer is never activated.
///
/// The discriminating factor is how quickly the front key is released after the other key was
/// pressed. Deliberately typing an accent means holding SPACE down while pressing the letter, so
/// its release comes much later than OVERLAP_REMOVAL_TIME. Mistyping means the two happen almost
/// simultaneously.
///
/// \param event      The front event of the queue. Must be a press of an overlap-eligible key.
/// \param scan       Result of scanning the queue after the front event.
/// \param now        Current time in micro-seconds.
inline OverlapAction evaluateOverlap(const Event& event, const OverlapScan& scan, unsigned long now) {
  // Without an intervening press the existing "on release" code already does the right thing.
  if (scan.m_interveningPressCount == 0) return OverlapAction::PROCESS;

  // More than one other key was pressed before the release: this is a deliberate use of the layer.
  if (scan.m_interveningPressCount > 1) return OverlapAction::PROCESS;

  if (!scan.m_foundRelease) {
    // The key is still held. Give the release a chance to arrive, but only for as long as the
    // overlap could still be considered accidental. This is the only latency we ever add.
    return (now - scan.m_firstInterveningPressTime < OVERLAP_REMOVAL_TIME) ? OverlapAction::WAIT : OverlapAction::PROCESS;
  }

  // A long hold is a deliberate use of the layer, whatever the release timing is.
  if (scan.m_releaseTime - event.m_time >= MAX_HOLD_TIME) return OverlapAction::PROCESS;

  // The release raced with the other key being pressed: the overlap was accidental.
  if (scan.m_releaseTime - scan.m_firstInterveningPressTime < OVERLAP_REMOVAL_TIME) {
    return OverlapAction::EMIT_TAP;
  }

  return OverlapAction::PROCESS;
}
