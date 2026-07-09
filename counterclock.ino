#include <M5Unified.h>
#include <Wire.h>

#include "CounterClockTimer.h"
#include "CounterClockFont.h"
#include "bitmaps.h"

namespace {

	// clock durations and timeout limits
	constexpr uint32_t PERIOD_MS = 30UL * 60UL * 1000UL; // 30:00
	constexpr uint32_t JAM_MS = 2UL * 60UL * 1000UL; // 2:00
	constexpr uint32_t LINEUP_MS = 30UL * 1000UL; // 0:30
	constexpr uint32_t OVERTIME_LINEUP_MS = 60UL * 1000UL; // 1:00
	constexpr uint32_t TIMEOUT_LIMIT_MS = 60UL * 60UL * 1000UL; // 1:00:00
	constexpr uint32_t HALFTIME_MS = 30UL * 60UL * 1000UL; // 30:00

	// shared display colors
	constexpr uint16_t COLOR_PINK = 0xE80E;
	constexpr uint16_t COLOR_PURPLE = 0x995A;
	constexpr uint16_t COLOR_MARINE = 0x0BF9;
	constexpr uint16_t COLOR_MINT = 0x062E;
	constexpr uint16_t COLOR_ORANGE = 0xFE20;
	constexpr uint16_t COLOR_DARK = 0x39C7;
	constexpr uint16_t COLOR_GRAY = 0x8410;

	// touch target positions, sizes, and hold durations
	constexpr int FEEDBACK_TOUCH_SIZE = 100;
	constexpr int TONE_TOUCH_X = 64;
	constexpr int TONE_TOUCH_Y = 76;
	constexpr int BUZZ_TOUCH_X = 342;
	constexpr int BUZZ_TOUCH_Y = 76;
	constexpr uint32_t FEEDBACK_HOLD_MS = 500;
	constexpr uint32_t TEAM_ICON_HOLD_MS = 500;
	constexpr int TEAM_LEFT_X = 56;
	constexpr int TEAM_RIGHT_X = 410;
	constexpr int TEAM_TOUCH_RADIUS = 36;
	constexpr int REVIEW_TOUCH_RADIUS = 52;
	constexpr int TIMEOUT_TOUCH_TOP = 134;
	constexpr int TIMEOUT_TOUCH_BOTTOM = 282;
	constexpr int REVIEW_TOUCH_TOP = 276;
	constexpr int REVIEW_TOUCH_BOTTOM = 372;
	constexpr int FIVE_SECOND_BUTTON_X = 210;
	constexpr int FIVE_SECOND_BUTTON_Y = 325;
	constexpr int FIVE_SECOND_BUTTON_WIDTH = 60;
	constexpr int FIVE_SECOND_BUTTON_HEIGHT = 60;
	constexpr int OVERTIME_JAM_BUTTON_X = 190;
	constexpr int OVERTIME_JAM_BUTTON_Y = 325;
	constexpr int OVERTIME_JAM_BUTTON_WIDTH = 100;
	constexpr int OVERTIME_JAM_BUTTON_HEIGHT = 60;

	// top level states of a game
	enum class GameState {
		WAIT,
		LINEUP,
		JAM,
		TIMEOUT,
		HALFTIME,
		END,
	};

	// types of timeout that share the timeout view
	enum class TimeoutMode {
		OFFICIAL,
		TEAM,
		REVIEW,
	};

	// editable values shown over the running timeout view
	enum class AdjustmentOverlay {
		NONE,
		PERIOD_CLOCK,
		GAME_NUMBERS,
		RTC_TIME,
	};

	// distinct tone and vibration patterns
	enum class FeedbackSignal {
		NONE,
		ROLLING_WHISTLE,
		JAM_START,
		TIME_WARNING,
		TIMEOUT,
		JAM_END,
	};

	// hardware and touch events collected during one loop
	struct Input {
		bool primary = false;
		bool timeout = false;
		bool next = false;
		bool previous = false;
		bool swipeUp = false;
		bool swipeDown = false;
		bool toggleTone = false;
		bool toggleBuzz = false;
		int teamTimeout = -1;
		int teamReview = -1;
		AdjustmentOverlay openOverlay = AdjustmentOverlay::NONE;
		bool overlayTap = false;
		int overlayX = 0;
		int overlayY = 0;
		bool fiveSecondLineup = false;
		bool startOvertimeJam = false;
	};

	// independent clocks used by each game state
	CounterClockTimer periodClock(PERIOD_MS);
	CounterClockTimer jamClock(JAM_MS);
	CounterClockTimer lineupClock(LINEUP_MS);
	CounterClockTimer timeoutClock(TIMEOUT_LIMIT_MS);
	CounterClockTimer halftimeClock(HALFTIME_MS);

	// off-screen canvas pushed to display each frame
	M5Canvas canvas(&M5.Display);

	// current game state, clocks, and overlay state
	GameState state = GameState::WAIT;
	TimeoutMode timeoutMode = TimeoutMode::OFFICIAL;
	AdjustmentOverlay adjustmentOverlay = AdjustmentOverlay::NONE;
	int stagedRtcHour = 0;
	int stagedRtcMinute = 0;
	int period = 0;
	int jam = 0;
	uint32_t remainingAtJamEnd = 0;
	bool overtime = false;

	// team timeout and review availability, including pending ones
	int teamTimeouts[2] = {3, 3};
	bool reviewAvailable[2] = {true, true};
	bool reviewRetained[2] = {false, false};
	int reviewTeam = -1;
	bool pendingSelection = false;
	bool pendingReviewRetained = false;
	bool toneEnabled = true;
	bool buzzEnabled = true;

	// active and queued feedback patterns plus screen-flash state
	FeedbackSignal feedbackSignal = FeedbackSignal::NONE;
	FeedbackSignal queuedFeedbackSignal = FeedbackSignal::NONE;
	FeedbackSignal queuedFeedbackSignal2 = FeedbackSignal::NONE;
	uint8_t feedbackStep = 0;
	uint32_t feedbackNextAt = 0;
	uint16_t screenFlashColor = BLACK;
	uint32_t screenFlashUntil = 0;

	// touch gesture tracking and one-shot warning state
	bool touchStarted = false;
	bool touchHoldHandled = false;
	int touchStartX = 0;
	int touchStartY = 0;
	uint32_t touchStartAt = 0;
	uint32_t lineupStartedAt = 0;
	bool periodZeroWarningSent = false;

	// display refresh throttle
	uint32_t lastDrawAt = 0;

	// return the status label for the active state and timeout type
	const char* statusLabel() {
		switch (state) {
			case GameState::WAIT: return "PUSH RIGHT BUTTON"; // "PRESS BUTTON TO START"
			case GameState::LINEUP: return overtime ? "OVERTIME LINEUP" : "LINEUP";
			case GameState::JAM: return overtime ? "OVERTIME JAM" : "JAM";
			case GameState::TIMEOUT:
				switch (timeoutMode) {
					case TimeoutMode::OFFICIAL: return "OFFICIAL TIMEOUT";
					case TimeoutMode::TEAM: return "TEAM TIMEOUT";
					case TimeoutMode::REVIEW: return "OFFICIAL REVIEW";
				}
			case GameState::HALFTIME: return "HALFTIME";
			case GameState::END: return "END OF GAME";
		}
		return "FORGOT LABEL"; // sorry
	};

	// return the accent color for the active game state
	uint16_t statusColor() {
		switch (state) {
			case GameState::LINEUP: return COLOR_ORANGE;
			case GameState::JAM: return COLOR_MINT;
			case GameState::TIMEOUT: return COLOR_PURPLE;
			case GameState::HALFTIME: return COLOR_MARINE;
			case GameState::WAIT:
			case GameState::END: return COLOR_PINK;
		}
		return WHITE;
	};

	// start a feedback pattern from its first step
	void startFeedback(FeedbackSignal signal) {
		feedbackSignal = signal;
		feedbackStep = 0;
		feedbackNextAt = 0;
	}

	// queue feedback: important signals play in order
	void queueFeedback(FeedbackSignal signal) {
		if (feedbackSignal == FeedbackSignal::NONE) {
			startFeedback(signal);
		} else if (queuedFeedbackSignal == FeedbackSignal::NONE) {
			queuedFeedbackSignal = signal;
		} else {
			queuedFeedbackSignal2 = signal;
		}
	};

	// feedback pattern shortcuts
	void rollingWhistle() { startFeedback(FeedbackSignal::ROLLING_WHISTLE); }
	void jamStartSignal() { startFeedback(FeedbackSignal::JAM_START); }
	void timeWarningSignal() { startFeedback(FeedbackSignal::TIME_WARNING); }
	void timeoutSignal() { startFeedback(FeedbackSignal::TIMEOUT); }
	void jamEndSignal() { startFeedback(FeedbackSignal::JAM_END); }

	// advance the current tone and vibration pattern without blocking the clocks
	void updateFeedback() {
		if (feedbackSignal == FeedbackSignal::NONE || millis() < feedbackNextAt) return;

		// pattern arrays define vibration level, step duration, and tone frequency
		static const uint8_t jamStart[]      = { 220, 0 };
		static const uint16_t jamStartTime[] = { 120, 1 };
		static const uint16_t jamStartTone[] = { 2200, 0 };
		static const uint8_t warning[]       = { 210, 0 };
		static const uint16_t warningTime[]  = { 160, 1 };
		static const uint16_t warningTone[]  = { 1100, 0 };
		static const uint8_t timeoutPulse[]  = { 240, 0, 240, 0, 240, 0, 240, 0 };
		static const uint16_t timeoutTime[]  = { 300, 100, 300, 100, 300, 100, 300, 1 };
		static const uint16_t timeoutTone[]  = { 1800, 0, 1800, 0, 1800, 0, 1800, 0 };
		static const uint8_t jamEnd[]        = { 220, 0, 220, 0, 220, 0, 220, 0 };
		static const uint16_t jamEndTime[]   = { 80, 50, 80, 50, 80, 50, 80, 1 };
		static const uint16_t jamEndTone[]   = { 2200, 0, 2200, 0, 2200, 0, 2200, 0 };
		static const uint8_t rolling[]       = { 110, 145, 180, 220, 180, 145, 110, 0 };
		static const uint16_t rollingTime[]  = { 45, 45, 45, 80, 45, 45, 90, 1 };
		static const uint16_t rollingTone[]  = { 2000, 1850, 1700, 1500, 1700, 1850, 2000, 0 };

		// select the arrays belonging to the active signal
		const uint8_t* levels = warning;
		const uint16_t* times = warningTime;
		const uint16_t* tones = warningTone;
		uint8_t length = sizeof(warning);
		switch (feedbackSignal) {
			case FeedbackSignal::ROLLING_WHISTLE:
				levels = rolling;
				times = rollingTime;
				tones = rollingTone;
				length = sizeof(rolling);
				break;
			case FeedbackSignal::JAM_START:
				levels = jamStart;
				times = jamStartTime;
				tones = jamStartTone;
				length = sizeof(jamStart);
				break;
			case FeedbackSignal::TIMEOUT:
				levels = timeoutPulse;
				times = timeoutTime;
				tones = timeoutTone;
				length = sizeof(timeoutPulse);
				break;
			case FeedbackSignal::JAM_END:
				levels = jamEnd;
				times = jamEndTime;
				tones = jamEndTone;
				length = sizeof(jamEnd);
				break;
			case FeedbackSignal::TIME_WARNING:
			case FeedbackSignal::NONE:
				break;
		}

		// play this step, then start the next queued signal when complete
		M5.Power.setVibration(buzzEnabled ? levels[feedbackStep] : 0);
		if (toneEnabled && tones[feedbackStep]) {
			M5.Speaker.tone(tones[feedbackStep], times[feedbackStep], 0, true);
		} else {
			M5.Speaker.stop(0);
		}
		feedbackNextAt = millis() + times[feedbackStep];
		if (++feedbackStep >= length) {
			feedbackSignal = FeedbackSignal::NONE;
			M5.Power.setVibration(0);
			M5.Speaker.stop(0);
			if (queuedFeedbackSignal != FeedbackSignal::NONE) {
				const FeedbackSignal next = queuedFeedbackSignal;
				queuedFeedbackSignal = queuedFeedbackSignal2;
				queuedFeedbackSignal2 = FeedbackSignal::NONE;
				startFeedback(next);
			}
		}
	};

	// resolve a touch x-coordinate to the nearby left or right team
	int teamAtTouchX(int x, int radius) {
		if (abs(x - TEAM_LEFT_X) <= radius) return 0;
		if (abs(x - TEAM_RIGHT_X) <= radius) return 1;
		return -1;
	};

	// convert physical buttons and touch gestures into one frame of input
	Input readInput() {
		Input input;

		// yellow button: timeout, blue button: next logical action
		input.timeout = M5.BtnA.wasPressed();
		input.next = input.timeout;
		input.primary = M5.BtnB.wasPressed();
		input.previous = M5.BtnB.wasHold() && M5.BtnB.wasReleased();

		// record the origin and start time of a new touch
		auto touch = M5.Touch.getDetail();
		if (touch.wasPressed()) {
			touchStarted = true;
			touchHoldHandled = false;
			touchStartX = touch.x;
			touchStartY = touch.y;
			touchStartAt = millis();
		}

		// trigger long-hold controls while the finger remains down
		if (touchStarted && !touchHoldHandled && touch.isPressed() &&
				abs(touch.x - touchStartX) < 30 && abs(touch.y - touchStartY) < 30) {
			const uint32_t heldFor = millis() - touchStartAt;
			const int timeoutTeam = teamAtTouchX(touchStartX, TEAM_TOUCH_RADIUS);
			const int reviewIconTeam = teamAtTouchX(touchStartX, REVIEW_TOUCH_RADIUS);

			// open timeout adjustment overlays from their displayed values
			if (adjustmentOverlay == AdjustmentOverlay::NONE &&
					state == GameState::TIMEOUT && heldFor >= TEAM_ICON_HOLD_MS &&
					touchStartX >= 125 && touchStartX <= 341 &&
					touchStartY >= 78 && touchStartY <= 145) {
				input.openOverlay = AdjustmentOverlay::PERIOD_CLOCK;
				touchHoldHandled = true;
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								state == GameState::TIMEOUT && heldFor >= TEAM_ICON_HOLD_MS &&
								touchStartX >= 135 && touchStartX <= 331 &&
								touchStartY >= 30 && touchStartY <= 76) {
				input.openOverlay = AdjustmentOverlay::GAME_NUMBERS;
				touchHoldHandled = true;
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								state == GameState::TIMEOUT && heldFor >= TEAM_ICON_HOLD_MS &&
								touchStartX >= 125 && touchStartX <= 341 &&
								touchStartY >= 394 && touchStartY <= 458) {
				input.openOverlay = AdjustmentOverlay::RTC_TIME;
				touchHoldHandled = true;
			// toggle sound and vibration from their status icons
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								heldFor >= FEEDBACK_HOLD_MS &&
					touchStartX >= (TONE_TOUCH_X-20) &&
					touchStartX < (TONE_TOUCH_X+20) + FEEDBACK_TOUCH_SIZE &&
					touchStartY >= (TONE_TOUCH_Y-20) &&
					touchStartY < (TONE_TOUCH_Y+20) + FEEDBACK_TOUCH_SIZE) {
				input.toggleTone = true;
				touchHoldHandled = true;
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								heldFor >= FEEDBACK_HOLD_MS &&
								touchStartX >= (BUZZ_TOUCH_X-20) &&
								touchStartX < (BUZZ_TOUCH_X+20) + FEEDBACK_TOUCH_SIZE &&
								touchStartY >= (BUZZ_TOUCH_Y-20) &&
								touchStartY < (BUZZ_TOUCH_Y+20) + FEEDBACK_TOUCH_SIZE) {
				input.toggleBuzz = true;
				touchHoldHandled = true;
			// select the team timeout or review icon being held
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								state == GameState::TIMEOUT &&
								heldFor >= TEAM_ICON_HOLD_MS && timeoutTeam >= 0 &&
								touchStartY >= TIMEOUT_TOUCH_TOP &&
								touchStartY <= TIMEOUT_TOUCH_BOTTOM) {
				input.teamTimeout = timeoutTeam;
				touchHoldHandled = true;
			} else if (adjustmentOverlay == AdjustmentOverlay::NONE &&
								state == GameState::TIMEOUT &&
								heldFor >= TEAM_ICON_HOLD_MS && reviewIconTeam >= 0 &&
								(reviewAvailable[reviewIconTeam] ||
									(pendingSelection && timeoutMode == TimeoutMode::REVIEW &&
									reviewTeam == reviewIconTeam)) &&
								touchStartY >= REVIEW_TOUCH_TOP &&
								touchStartY <= REVIEW_TOUCH_BOTTOM) {
				input.teamReview = reviewIconTeam;
				touchHoldHandled = true;
			}
		}

		// interpret short taps and swipes when the touch is released
		if (touchStarted && touch.wasReleased()) {
			touchStarted = false;
			const int dx = touch.x - touchStartX;
			const int dy = touch.y - touchStartY;
			const uint32_t duration = millis() - touchStartAt;

			// send taps to an open adjustment overlay
			if (!touchHoldHandled && adjustmentOverlay != AdjustmentOverlay::NONE &&
					duration < 900 && abs(dx) < 30 && abs(dy) < 30) {
				input.overlayTap = true;
				input.overlayX = touch.x;
				input.overlayY = touch.y;
			// activate the contextual five-second or overtime button
			} else if (!touchHoldHandled && adjustmentOverlay == AdjustmentOverlay::NONE &&
								(state == GameState::LINEUP || state == GameState::END) && duration < 900 &&
								abs(dx) < 30 && abs(dy) < 30 &&
								touchStartX >= FIVE_SECOND_BUTTON_X - 15 &&
								touchStartX <= FIVE_SECOND_BUTTON_X + FIVE_SECOND_BUTTON_WIDTH + 15 &&
								touchStartY >= FIVE_SECOND_BUTTON_Y - 15 &&
								touchStartY <= FIVE_SECOND_BUTTON_Y + FIVE_SECOND_BUTTON_HEIGHT + 50) {
				if (state == GameState::LINEUP) {
					input.fiveSecondLineup = true;
				} else {
					input.startOvertimeJam = true;
				}
			// preserve the lineup swipe shortcut for setting five seconds
			} else if (!touchHoldHandled && adjustmentOverlay == AdjustmentOverlay::NONE &&
								duration < 900 && (abs(dx) > 70 || abs(dy) > 70)) {
				if (abs(dy) > abs(dx)) {
					input.swipeUp = dy < 0;
					input.swipeDown = dy > 0;
				}
			}
		}

		return input;
	}

	void beginOfficialTimeout();

	// cancel the uncommitted timeout or review selection
	void clearPendingTimeoutSelection() {
		pendingSelection = false;
		pendingReviewRetained = false;
		reviewTeam = -1;
	};

	// apply tone and vibration toggles, with feedback flash
	void updateFeedbackSettings(const Input& input) {
		if (adjustmentOverlay != AdjustmentOverlay::NONE) return;
		if (input.toggleTone) {
			toneEnabled = !toneEnabled;
			screenFlashColor = toneEnabled ? GREEN : RED;
			screenFlashUntil = millis() + 350;
			if (toneEnabled) {
				M5.Speaker.tone(1000, 120);
			} else {
				M5.Speaker.stop();
			}
		}
		if (input.toggleBuzz) {
			buzzEnabled = !buzzEnabled;
			screenFlashColor = buzzEnabled ? GREEN : RED;
			screenFlashUntil = millis() + 350;
			if (buzzEnabled) {
				M5.Power.setVibration(200);
				delay(120);
			}
			M5.Power.setVibration(0);
		}
	};

	// make one team timeout the only pending timeout selection
	void beginTeamTimeout(int team) {
		if (state != GameState::TIMEOUT) return;
		if (teamTimeouts[team] <= 0) {
			screenFlashColor = RED;
			screenFlashUntil = millis() + 350;
			return;
		}

		clearPendingTimeoutSelection();
		timeoutMode = TimeoutMode::TEAM;
		reviewTeam = team;
		pendingSelection = true;
		screenFlashColor = GREEN;
		screenFlashUntil = millis() + 350;
	};

	// select a teams review or toggle the active review between lost and retained
	void beginOrToggleReview(int team) {
		if (state != GameState::TIMEOUT) return;
		const bool sameActiveReview = pendingSelection && timeoutMode == TimeoutMode::REVIEW && reviewTeam == team;
		if (!sameActiveReview && !reviewAvailable[team]) {
			screenFlashColor = RED;
			screenFlashUntil = millis() + 350;
			return;
		}

		if (!sameActiveReview) {
			clearPendingTimeoutSelection();
			timeoutMode = TimeoutMode::REVIEW;
			reviewTeam = team;
			pendingSelection = true;
			screenFlashColor = RED;
		} else {
			pendingReviewRetained = !pendingReviewRetained;
			screenFlashColor = pendingReviewRetained ? GREEN : RED;
		}
		screenFlashUntil = millis() + 350;
	};

	// apply team-icon input only while in timeout state
	void updateTeamIconControls(const Input& input) {
		if (state != GameState::TIMEOUT) return;
		if (input.teamTimeout >= 0) beginTeamTimeout(input.teamTimeout);
		if (input.teamReview >= 0) beginOrToggleReview(input.teamReview);
	};

	// open an adjustment overlay and stage the current rtc time when changed
	void openAdjustmentOverlay(AdjustmentOverlay overlay) {
		adjustmentOverlay = overlay;
		if (overlay == AdjustmentOverlay::RTC_TIME && M5.Rtc.isEnabled()) {
			const auto time = M5.Rtc.getTime();
			stagedRtcHour = time.hours;
			stagedRtcMinute = time.minutes;
		}
	};

	// write the set tim to rtc clock
	void commitRtcTime() {
		if (M5.Rtc.isEnabled()) {
			M5.Rtc.setTime({static_cast<int8_t>(stagedRtcHour), static_cast<int8_t>(stagedRtcMinute), 0});
		}
	};

	// handle taps in an adjustment overlay while clocks continue running
	void updateAdjustmentOverlay(const Input& input) {
		// close overlays that are no longer inside a timeout
		if (state != GameState::TIMEOUT) {
			adjustmentOverlay = AdjustmentOverlay::NONE;
			return;
		}

		// open the requested overlay and keep normal timeout behavior otherwise
		if (input.openOverlay != AdjustmentOverlay::NONE) {
			openAdjustmentOverlay(input.openOverlay);
		}
		if (adjustmentOverlay == AdjustmentOverlay::NONE) return;

		// physical buttons dismiss the overlay without changing the rtc
		if (input.primary || input.timeout) {
			adjustmentOverlay = AdjustmentOverlay::NONE;
			return;
		}
		if (!input.overlayTap) return;

		// finish or cancel from the bottom action buttons
		if (input.overlayY >= 360) {
			const bool cancelRtc = adjustmentOverlay == AdjustmentOverlay::RTC_TIME && input.overlayX < canvas.width() / 2;
			if (adjustmentOverlay == AdjustmentOverlay::RTC_TIME && !cancelRtc) commitRtcTime();
			adjustmentOverlay = AdjustmentOverlay::NONE;
			return;
		}

		// apply plus or minus to the selected column
		const bool leftColumn = input.overlayX < canvas.width() / 2;
		int direction = 0;
		if (input.overlayY >= 140 && input.overlayY <= 212) direction = 1;
		if (input.overlayY >= 282 && input.overlayY <= 354) direction = -1;
		if (direction == 0) return;
		switch (adjustmentOverlay) {
			case AdjustmentOverlay::PERIOD_CLOCK:
				periodClock.adjustRemaining(direction * (leftColumn ? 60000 : 1000));
				if (!periodClock.done()) periodZeroWarningSent = false;
				break;
			case AdjustmentOverlay::GAME_NUMBERS:
				if (leftColumn) {
					period = constrain(period + direction, 1, 2);
				} else {
					jam = constrain(jam + direction, 0, 99);
				}
				break;
			case AdjustmentOverlay::RTC_TIME:
				if (leftColumn) {
					stagedRtcHour = (stagedRtcHour + direction + 24) % 24;
				} else {
					stagedRtcMinute = (stagedRtcMinute + direction + 60) % 60;
				}
				break;
			case AdjustmentOverlay::NONE:
				break;
		}
	};

	// commit the single pending timeout or review when leaving timeout mode
	void finalizeTimeoutChanges() {
		if (pendingSelection && reviewTeam >= 0) {
			if (timeoutMode == TimeoutMode::TEAM && teamTimeouts[reviewTeam] > 0) {
				--teamTimeouts[reviewTeam];
			} else if (timeoutMode == TimeoutMode::REVIEW) {
				reviewAvailable[reviewTeam] = pendingReviewRetained;
				reviewRetained[reviewTeam] = pendingReviewRetained;
			}
		}
		clearPendingTimeoutSelection();
	};

	// restore all game clocks, timeouts, and ui state
	void resetGame() {
		periodClock.reset();
		jamClock.reset();
		lineupClock.setDuration(LINEUP_MS);
		lineupClock.reset();
		timeoutClock.reset();
		halftimeClock.reset();
		period = 0;
		jam = 0;
		remainingAtJamEnd = 0;
		overtime = false;
		teamTimeouts[0] = teamTimeouts[1] = 3;
		reviewAvailable[0] = reviewAvailable[1] = true;
		reviewRetained[0] = reviewRetained[1] = false;
		clearPendingTimeoutSelection();
		adjustmentOverlay = AdjustmentOverlay::NONE;
		periodZeroWarningSent = false;
		timeoutMode = TimeoutMode::OFFICIAL;
		state = GameState::WAIT;
	};

	// start lineup, optionally resuming the period clock
	void beginLineup(bool resumePeriod = false) {
		state = GameState::LINEUP;
		lineupStartedAt = millis();
		lineupClock.reset();
		lineupClock.start();
		if (resumePeriod) periodClock.start(); // FIXME check if this is accurate. if period clock is stopped, it should start at jam start. hat was i thinking back then?
	};

	// move to halftime or end-of-game and signal the transition
	void finishPeriod() {
		state = period == 1 ? GameState::HALFTIME : GameState::END;
		halftimeClock.reset();
		if (state == GameState::HALFTIME) halftimeClock.start();
		queueFeedback(FeedbackSignal::ROLLING_WHISTLE);
	};

	// pause play and enter an official timeout before a team/type is selected
	void beginOfficialTimeout() {
		remainingAtJamEnd = periodClock.remainingMs();
		periodClock.pause();
		lineupClock.reset();
		jamClock.reset();
		timeoutClock.reset();
		timeoutClock.start();
		timeoutMode = TimeoutMode::OFFICIAL;
		clearPendingTimeoutSelection();
		state = GameState::TIMEOUT;
		timeoutSignal();
	};

	// commit timeout changes and return to lineup or finish the period
	void resumeFromTimeout() {
		finalizeTimeoutChanges();
		adjustmentOverlay = AdjustmentOverlay::NONE;
		timeoutClock.reset();
		reviewTeam = -1;
		if (periodClock.done()) {
			finishPeriod();
		} else {
			beginLineup(timeoutMode == TimeoutMode::OFFICIAL && remainingAtJamEnd < LINEUP_MS);
			rollingWhistle();
		}
	};

	// advance the game state depending on inputs and events
	void updateState(const Input& input) {

		// keep timeout limits and warnings active behind an adjustment overlay
		if (state == GameState::TIMEOUT && (adjustmentOverlay != AdjustmentOverlay::NONE || input.openOverlay != AdjustmentOverlay::NONE)) {
			updateAdjustmentOverlay(input);
			if (timeoutMode == TimeoutMode::TEAM && timeoutClock.elapsedSeconds() >= 60) {
				resumeFromTimeout();
			}
			if (timeoutClock.ticked()) {
				const uint32_t displayedElapsed = (timeoutClock.elapsedMs() + 999) / 1000;
				if (displayedElapsed == 50) timeWarningSignal();
			}
			return;
		}

		switch (state) {
			// start the first lineup from the ready screen
			case GameState::WAIT:
				if (input.primary) {
					period = 1;
					beginLineup();
					rollingWhistle();
				}
			break;

			// accept lineup shortcuts, warnings, and the transition into a jam
			case GameState::LINEUP: {
				const bool fiveSecondShortcutAvailable = periodClock.paused() || jam == 0 || periodClock.remainingMs() < LINEUP_MS;
				if (input.timeout) {
					beginOfficialTimeout();
				} else if ((input.swipeDown || input.previous || input.fiveSecondLineup) && lineupClock.remainingMs() > 5000 && fiveSecondShortcutAvailable) {
					lineupClock.setRemaining(5000);
				} else if (input.primary) {
					if (lineupClock.remainingMs() > 5000 && fiveSecondShortcutAvailable) {
						lineupClock.setRemaining(5000);
					} else {
						lineupClock.finish();
					}
				}

				if (periodClock.done() && !overtime) {
					finishPeriod();
				} else if (lineupClock.justReachedSecond(6) || lineupClock.justReachedSecond(5) || lineupClock.justReachedSecond(4) || lineupClock.justReachedSecond(3) || lineupClock.justReachedSecond(2) || lineupClock.justReachedSecond(1)) { // FIXME find a more elegant way to check this
					timeWarningSignal();
				}

				if (state == GameState::LINEUP && lineupClock.done()) {
					state = GameState::JAM;
					jamClock.reset();
					jamClock.start();
					periodClock.start();
					++jam;
					jamStartSignal();
				}
				break;
			}

			// handle jam controls, final warning, and the next state
			case GameState::JAM:
				if (input.timeout) {
					if (jamClock.elapsedMs() < 3000 && jam > 0) --jam;
					beginOfficialTimeout();
				} else if (input.primary && !overtime && jamClock.elapsedMs() >= 3000) {
					jamClock.finish();
				}

				if (!jamClock.done() && jamClock.justReachedSecond(5)) {
					timeWarningSignal();
				}

				if (state == GameState::JAM && jamClock.done()) {
					remainingAtJamEnd = periodClock.remainingMs();
					if (periodClock.done()) {
						finishPeriod();
					} else {
						beginLineup();
						jamEndSignal();
					}
				}
			break;

			// change timeout type, enforce limits, and emit the 50-second warning
			case GameState::TIMEOUT: // {
				if (input.next) {
					clearPendingTimeoutSelection();
					timeoutMode = static_cast<TimeoutMode>((static_cast<int>(timeoutMode) + 1) % 3);
				}
				if (input.previous) {
					clearPendingTimeoutSelection();
					timeoutMode = static_cast<TimeoutMode>((static_cast<int>(timeoutMode) + 2) % 3);
				}

				switch (timeoutMode) {
					case TimeoutMode::OFFICIAL:
						if (input.primary) resumeFromTimeout();
					break;
					case TimeoutMode::TEAM:
						if (timeoutClock.elapsedSeconds() >= 60) resumeFromTimeout();
					break;
					case TimeoutMode::REVIEW:
						if (input.primary) resumeFromTimeout();
					break;
				}

				if (timeoutClock.ticked()) {
					// the elapsed clock is displayed with a ceiling, so warn as it changes to 0:50
					const uint32_t displayedElapsed = (timeoutClock.elapsedMs() + 999) / 1000;
					if (displayedElapsed == 50) timeWarningSignal();
				}
			break;
			// }

			// start period two and restore both teams' reviews
			case GameState::HALFTIME:
				if (input.timeout) {
					beginOfficialTimeout();
				} else if (input.primary) {
					halftimeClock.reset();
					periodClock.reset();
					periodZeroWarningSent = false;
					lineupClock.setDuration(LINEUP_MS);
					++period;
					jam = 0;
					overtime = false;
					reviewAvailable[0] = reviewAvailable[1] = true;
					reviewRetained[0] = reviewRetained[1] = false;
					beginLineup();
				}
			break;

			// reset the game or start an overtime lineup from the final screen
			case GameState::END:
				if (input.timeout) {
					beginOfficialTimeout();
				} else if (input.primary) {
					resetGame();
					rollingWhistle();
				} else if (input.previous || input.startOvertimeJam) {
					overtime = true;
					lineupClock.setDuration(OVERTIME_LINEUP_MS);
					beginLineup();
					rollingWhistle();
				}
			break;
		}
	};

	// draw centered text using the original counter clock pixel font
	void drawCounterClockText(const char* text, int y, uint16_t color, int size) {
		canvas.setFont(&CounterClockFont);
		canvas.setTextSize(size);
		canvas.setTextColor(color);

		// FIXME this api does not center the CounterClockFont properly
		// canvas.settextdatum(middle_center);
		// canvas.drawstring(text, canvas.width() / 2, y);
		// this is a workaround:
		canvas.setTextDatum(middle_left);
		canvas.drawString(text, (canvas.width()-canvas.textWidth(text, &CounterClockFont))/2+(size*3-2), y); // screen is 466x466
	};

	// draw a centered status label using the readable system pixel font
	void drawStatusText(const char* text, int y, uint16_t color, int size = 2) {
		canvas.setFont(&fonts::AsciiFont8x16);
		canvas.setTextSize(size);
		canvas.setTextColor(color);
		canvas.setTextDatum(middle_center);
		canvas.drawString(text, canvas.width() / 2, y);
	};

	// format milliseconds as a ceiling-rounded mm:ss clock
	void drawClock(uint32_t milliseconds, int y, uint16_t color, int size) {
		const uint32_t seconds = (milliseconds + 999) / 1000;
		char text[12];
		snprintf(text, sizeof(text), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
		drawCounterClockText(text, y, color, size);
	};

	// draw battery icon
	void drawBatteryIcon() {
		const int batteryLevel = constrain(M5.Power.getBatteryLevel(), 0, 100);
		const bool charging = M5.Power.isCharging();
		constexpr int width = 34;
		constexpr int height = 16;
		const int x = (canvas.width() - width) / 2;
		constexpr int y = 30;

		// hide every other frame when the battery is critically low
		if (!charging && batteryLevel < 15 && (millis() % 1000) >= 500) return;

		// choose the battery color from its current charge level
		uint16_t color = batteryLevel < 15 ? RED : (batteryLevel < 50 ? COLOR_ORANGE : COLOR_MINT);
		if (charging) color = COLOR_MINT;

		canvas.drawRoundRect(x, y, width - 4, height, 3, color);
		canvas.fillRoundRect(x + width - 4, y + 4, 4, height - 8, 1, color);

		// fill the icon proportionally to the reported charge
		const int fillWidth = map(batteryLevel, 0, 100, 0, width - 10);
		if (fillWidth > 0) {
			canvas.fillRect(x + 3, y + 3, fillWidth, height - 6, color);
		}

		// mark charging with a plus sign
		if (charging) {
			canvas.setFont(&CounterClockFont); // fixme: bitmap
			canvas.setTextFont(1);
			canvas.setTextColor(BLACK);
			canvas.drawString("+", x + (width - 4) / 2, y + height / 2);
		}
	};

	// draw bitmap with position, scale, color
	void drawBitmap(int x, int y, const unsigned char* bitmap, int width, int height, int scale, uint16_t color) {
		const int bytesPerRow = (width + 7) / 8;
		for (int row = 0; row < height; ++row) {
			for (int column = 0; column < width; ++column) {
				const uint8_t value = pgm_read_byte(bitmap + row * bytesPerRow + column / 8);
				if (value & (0x80 >> (column & 7))) {
					canvas.fillRect(x + column * scale, y + row * scale, scale, scale, color);
				}
			}
		}
	};

	// draw the current tone and vibration toggle states
	void drawFeedbackIcons() {
		constexpr int iconScale = 2;
		constexpr int toneWidth = 18;
		constexpr int buzzWidth = 17;
		constexpr int iconHeight = 16;
		const uint16_t toneColor = toneEnabled ? COLOR_MINT : COLOR_DARK;
		const uint16_t buzzColor = buzzEnabled ? COLOR_MINT : COLOR_DARK;

		drawBitmap(TONE_TOUCH_X + 12, TONE_TOUCH_Y + 14, toneEnabled ? tone_on : tone_off, toneWidth, iconHeight, iconScale, toneColor);
		drawBitmap(BUZZ_TOUCH_X + 13, BUZZ_TOUCH_Y + 14, buzz_on, buzzWidth, iconHeight, iconScale, buzzColor);
		if (!buzzEnabled) {
			drawBitmap(BUZZ_TOUCH_X + 13, BUZZ_TOUCH_Y + 29, buzz_off, buzzWidth, 1, iconScale, COLOR_PINK);
		}
	}

	// draw remaining timeouts and review
	void drawTeamIndicators(int x, int direction, int team) {

		// determine if pending timeout or review (should blink)
		const bool blinkOn = (millis() % 1000) >= 500;
		const bool teamTimeoutSelected = state == GameState::TIMEOUT && pendingSelection && timeoutMode == TimeoutMode::TEAM && reviewTeam == team;
		const bool reviewSelected = state == GameState::TIMEOUT && pendingSelection && timeoutMode == TimeoutMode::REVIEW && reviewTeam == team;
		constexpr int iconScale = 3;
		constexpr int iconSize = 9 * iconScale;
		const int iconX = x - iconSize / 2;

		// draw all available timeouts, blinking the pending one
		for (int i = 0; i < teamTimeouts[team]; ++i) {
			// the lowest visible timeout is the one currently being consumed
			const bool blinkThisTimeout = teamTimeoutSelected && i == teamTimeouts[team] - 1;
			if (!blinkThisTimeout || blinkOn) {
				drawBitmap(iconX, 150 + i * 48, timeout, 9, 9, iconScale, COLOR_DARK);
			}
		}

		// draw an available or pending review with its current outcome
		if ((reviewAvailable[team] || reviewSelected) && (!reviewSelected || blinkOn)) {
			const bool retainedState = reviewSelected ? pendingReviewRetained : reviewRetained[team];
			const uint16_t reviewColor = reviewSelected ? (retainedState ? COLOR_MINT : COLOR_ORANGE) : COLOR_DARK;
			drawBitmap(iconX, 294, retainedState ? retained : review, 9, 9, iconScale, reviewColor);
		}

		/* FIXME: home / guest? team names from crg?
		canvas.setFont(&fonts::AsciiFont8x16);
		canvas.setTextSize(1);
		canvas.setTextColor(COLOR_DARK);
		canvas.setTextDatum(middle_center);
		canvas.drawstring(direction < 0 ? "a" : "b", x, 365); // a or b
		*/
	};

	// draw the hardware rtc time and a small clock symbol
	void drawRtcTime() {
		char timeText[8] = "--:--";
		if (M5.Rtc.isEnabled()) {
			const auto time = M5.Rtc.getTime();
			if (time.hours >= 0 && time.minutes >= 0) {
				snprintf(timeText, sizeof(timeText), "%02d:%02d", time.hours, time.minutes);
			}
		}

		// clock icon
		constexpr int iconX = 180;
		constexpr int y = 420;
		canvas.drawCircle(iconX, y, 11, COLOR_DARK);
		canvas.drawLine(iconX, y, iconX, y - 6, COLOR_DARK);
		canvas.drawLine(iconX, y, iconX + 5, y + 3, COLOR_DARK);

		// hh:mm
		canvas.setFont(&CounterClockFont);
		canvas.setTextSize(5);
		canvas.setTextColor(COLOR_GRAY);
		canvas.setTextDatum(middle_left);
		canvas.drawString(timeText, iconX + 18, y+2);
	};

	// draw value adjustment ui with + and -
	// FIXME: keep cycling while touched
	void drawOverlayValue(const char* label, const char* value, int x) {
		canvas.setFont(&fonts::AsciiFont8x16);
		canvas.setTextSize(1);
		canvas.setTextColor(COLOR_GRAY);
		canvas.setTextDatum(middle_center);
		canvas.drawString(label, x, 120);

		canvas.fillRoundRect(x - 68, 140, 136, 72, 18, COLOR_MINT);
		canvas.setFont(&fonts::AsciiFont24x48);
		canvas.setTextSize(2);
		canvas.setTextColor(BLACK);
		canvas.drawString("+", x, 176);

		canvas.setFont(&CounterClockFont);
		canvas.setTextSize(10);
		canvas.setTextColor(WHITE);
		canvas.drawString(value, x, 248);

		canvas.fillRoundRect(x - 68, 282, 136, 72, 18, COLOR_PURPLE);
		canvas.setFont(&fonts::AsciiFont24x48);
		canvas.setTextSize(2);
		canvas.setTextColor(WHITE);
		canvas.drawString("-", x, 318);
	};

	// draw the overlay for changing the period clock, rtc clock, period and jam number
	void drawAdjustmentOverlay() {
		if (adjustmentOverlay == AdjustmentOverlay::NONE) return;

		// FIXME positioning this sucks
		canvas.fillRoundRect(34, 70, 398, 340, 42, BLACK);
		canvas.drawRoundRect(34, 70, 398, 340, 42, COLOR_PURPLE);
		canvas.drawFastVLine(canvas.width() / 2, 106, 250, COLOR_DARK);

		// prepare labels and values for the selected adjustment type
		const char* title = "";
		char leftValue[8];
		char rightValue[8];
		const char* leftLabel = "";
		const char* rightLabel = "";

		switch (adjustmentOverlay) {
			case AdjustmentOverlay::PERIOD_CLOCK: {
				title = "PERIOD CLOCK";
				leftLabel = "MINUTES";
				rightLabel = "SECONDS";
				const uint32_t seconds = (periodClock.remainingMs() + 999) / 1000;
				snprintf(leftValue, sizeof(leftValue), "%02lu", static_cast<unsigned long>(seconds / 60));
				snprintf(rightValue, sizeof(rightValue), "%02lu", static_cast<unsigned long>(seconds % 60));
				break;
			}
			case AdjustmentOverlay::GAME_NUMBERS:
				title = "GAME NUMBERS";
				leftLabel = "PERIOD";
				rightLabel = "JAM";
				snprintf(leftValue, sizeof(leftValue), "%d", period);
				snprintf(rightValue, sizeof(rightValue), "%02d", jam);
				break;
			case AdjustmentOverlay::RTC_TIME: {
				title = "REAL TIME";
				leftLabel = "HOURS";
				rightLabel = "MINUTES";
				snprintf(leftValue, sizeof(leftValue), "%02d", stagedRtcHour);
				snprintf(rightValue, sizeof(rightValue), "%02d", stagedRtcMinute);
				break;
			}
			case AdjustmentOverlay::NONE:
				return;
		}

		drawStatusText(title, 88, COLOR_PURPLE, 1);
		drawOverlayValue(leftLabel, leftValue, 145);
		drawOverlayValue(rightLabel, rightValue, 321);

		// buttons
		canvas.setFont(&fonts::AsciiFont8x16);
		canvas.setTextSize(1);
		canvas.setTextColor(WHITE);
		canvas.setTextDatum(middle_center);
		if (adjustmentOverlay == AdjustmentOverlay::RTC_TIME) { // rtc editing can be cancelled
			canvas.fillRoundRect(70, 365, 156, 34, 12, COLOR_PINK);
			canvas.fillRoundRect(240, 365, 156, 34, 12, COLOR_DARK);
			canvas.drawString("CANCEL", 148, 382);
			canvas.drawString("DONE", 318, 382);
		} else {
			canvas.fillRoundRect(145, 365, 176, 34, 12, COLOR_DARK);
			canvas.drawString("DONE", canvas.width() / 2, 382);
		}
	};

	// draw 5 second button when period clock is halted during lineup
	void drawFiveSecondButton() {
		if (state != GameState::LINEUP || adjustmentOverlay != AdjustmentOverlay::NONE) return;
		const bool enabled = periodClock.paused() || jam == 0 || periodClock.remainingMs() < LINEUP_MS;
		if (!enabled) return;

		// box
		canvas.fillRoundRect(FIVE_SECOND_BUTTON_X, FIVE_SECOND_BUTTON_Y, FIVE_SECOND_BUTTON_WIDTH, FIVE_SECOND_BUTTON_HEIGHT, 16, COLOR_ORANGE);

		/* use bitmap instead
		canvas.setFont(&CounterClockFont);
		canvas.setTextSize(5);
		canvas.setTextColor(BLACK);
		canvas.setTextDatum(middle_center);
		canvas.drawString("5", FIVE_SECOND_BUTTON_X + 78, FIVE_SECOND_BUTTON_Y + FIVE_SECOND_BUTTON_HEIGHT / 2);
		*/

		// down arrow bitmap
		drawBitmap(FIVE_SECOND_BUTTON_X + 18, FIVE_SECOND_BUTTON_Y + 6, downto5, 8, 16, 3, BLACK);

	}

	// draw overtime jam button at end of game, starts overtime jam
	void drawOvertimeButton() {
		if (state != GameState::END || adjustmentOverlay != AdjustmentOverlay::NONE) return;

		canvas.fillRoundRect(OVERTIME_JAM_BUTTON_X, OVERTIME_JAM_BUTTON_Y, OVERTIME_JAM_BUTTON_WIDTH, OVERTIME_JAM_BUTTON_HEIGHT, 16, COLOR_PURPLE);
		canvas.setFont(&fonts::AsciiFont8x16);
		canvas.setTextSize(1);
		canvas.setTextColor(WHITE);
		canvas.setTextDatum(middle_center);
		canvas.drawString("OVERTIME", OVERTIME_JAM_BUTTON_X + OVERTIME_JAM_BUTTON_WIDTH / 2, OVERTIME_JAM_BUTTON_Y + 21);
		canvas.drawString("JAM", OVERTIME_JAM_BUTTON_X + OVERTIME_JAM_BUTTON_WIDTH / 2, OVERTIME_JAM_BUTTON_Y + 40);
	};

	// draw a clockwise ring proportional to a clocks remaining time
	void drawRing(int innerRadius, int outerRadius, uint32_t remainingMs, uint32_t durationMs, uint16_t color) {
		if (durationMs == 0 || remainingMs == 0) return;
		const float fraction = min(remainingMs, durationMs) / static_cast<float>(durationMs);
		const float endAngle = 359.5f * fraction;
		canvas.fillArc(canvas.width() / 2, canvas.height() / 2, innerRadius, outerRadius, 270, 270+endAngle, color);
	};

	// draw the period ring and the current state inner ring
	void drawRings() {
		drawRing(226, 234, periodClock.remainingMs(), PERIOD_MS, COLOR_PINK);

		switch (state) {
			case GameState::LINEUP:
				drawRing(216, 224, lineupClock.remainingMs(), overtime ? OVERTIME_LINEUP_MS : LINEUP_MS, COLOR_ORANGE);
			break;
			case GameState::JAM:
				drawRing(216, 224, jamClock.remainingMs(), JAM_MS, COLOR_MINT);
			break;
			case GameState::TIMEOUT:
				if (timeoutMode == TimeoutMode::TEAM) {
					const uint32_t elapsed = timeoutClock.elapsedMs();
					const uint32_t remaining = elapsed < 60000 ? 60000 - elapsed : 0;
					drawRing(216, 224, remaining, 60000, COLOR_PURPLE);
				}
			break;
			case GameState::WAIT:
			case GameState::HALFTIME:
			case GameState::END:
				// these don't have rings
			break;
		}
	};

	// draw current screen frame
	void drawScreen() {

		// throttle drawing
		if (millis() - lastDrawAt < 80) return;
		lastDrawAt = millis();

		// replace the regular ui with feedback flash
		if (millis() < screenFlashUntil) {
			canvas.fillScreen(screenFlashColor);
			canvas.pushSprite(0, 0);
			return;
		}

		// background and rings
		canvas.fillScreen(BLACK);
		drawRings();

		// text anchor is middle (v) and center (h)
		canvas.setTextDatum(middle_center);

		// draw the period and jam number
		char top[20];
		snprintf(top, sizeof(top), "P%d  JAM %02d", period, jam);
		drawCounterClockText(top, 72, COLOR_GRAY, 5);

		// draw the period clock
		drawClock(periodClock.remainingMs(), 128, COLOR_GRAY, 8);

		// select the main clock for the active phase
		uint32_t mainTime = 0;
		if (state == GameState::LINEUP) mainTime = lineupClock.remainingMs();
		if (state == GameState::JAM) mainTime = jamClock.remainingMs();
		if (state == GameState::TIMEOUT) mainTime = timeoutClock.elapsedMs();
		if (state == GameState::HALFTIME) mainTime = halftimeClock.elapsedMs();

		// draw the main clock or the ready/final label
		if (state == GameState::WAIT || state == GameState::END) {
			drawCounterClockText(state == GameState::WAIT ? "READY" : "00:00", 235, statusColor(), 14);
		} else {
			drawClock(mainTime, 235, statusColor(), 14);
		}

		// draw the current status label
		drawStatusText(statusLabel(), 300, statusColor(), 2);

		// draw timeut / review indicators
		drawTeamIndicators(TEAM_LEFT_X, -1, 0);
		drawTeamIndicators(TEAM_RIGHT_X, 1, 1);

		// battery icon
		drawBatteryIcon();

		// feedback toggle icons
		drawFeedbackIcons();

		// realtime clock (this is buggy between restarts, investigate)
		drawRtcTime();

		// 5s (when clock halted) and otj button (end of game)
		drawFiveSecondButton();
		drawOvertimeButton();

		// adjustment overlays
		drawAdjustmentOverlay();

		// push to canvas
		canvas.pushSprite(0, 0);
	};

	// show splash screen, rolling whistle
	void showSplashScreen() {
		canvas.fillScreen(BLACK);
		canvas.setTextDatum(middle_center);
		drawCounterClockText("COUNTER", 190, COLOR_PINK, 11);
		drawCounterClockText("CLOCK", 280, COLOR_PURPLE, 11);
		canvas.pushSprite(0, 0);
		rollingWhistle();
		const uint32_t splashEndsAt = millis() + 1000;
		while (millis() < splashEndsAt) {
			updateFeedback();
			delay(5);
		}
	};

	// give time warning when period clock is down during jam
	void periodZeroWarning() {
		if (state == GameState::JAM && periodClock.done() && !periodZeroWarningSent) {
			periodZeroWarningSent = true;
			queueFeedback(FeedbackSignal::TIME_WARNING);
		}
	};

}

// initialize hardware, the display canvas, and the initial game state
void setup() {
	auto config = M5.config();
	config.internal_spk = true;
	M5.begin(config);
	Serial.begin(115200);
	M5.Speaker.setVolume(220);

	// create the full-screen sprite used for flicker-free drawing
	M5.Display.setRotation(0);
	canvas.setColorDepth(16);
	canvas.createSprite(M5.Display.width(), M5.Display.height());
	canvas.setTextWrap(false);

	showSplashScreen();
	resetGame();
	drawScreen();
}

// update hardware, clocks, game logic, feedback, and the display
void loop() {
	M5.update();

	// advance clocks
	periodClock.update();
	jamClock.update();
	lineupClock.update();
	timeoutClock.update();
	halftimeClock.update();

	// get inputs
	const Input input = readInput();

	// warning when period clock is down while jam is running
	periodZeroWarning();

	// toggle tone / vibration feedback
	updateFeedbackSettings(input);

	// activate T and O touch toggles during timeout
	updateTeamIconControls(input);

	// process game related inputs
	updateState(input);

	// execute tone / vibration feedback
	updateFeedback();

	// update screen
	drawScreen();

	delay(50); // ~20 tics per second
}
