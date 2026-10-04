#include "pch.h"

#include "idle_thread.h"
#include <cmath>

using ::testing::_;

namespace {
void configureRollingTrigger(EngineTestHelper& eth, int teeth = 24, bool isCam = true, bool twoStroke = false) {
	engineConfiguration->isIgnitionEnabled = false;
	engineConfiguration->isInjectionEnabled = false;
	engineConfiguration->idleTimingUseCycleRpm = false;
	engineConfiguration->idleTimingUseRollingRpm = true;
	engineConfiguration->skippedWheelOnCam = isCam;
	engineConfiguration->twoStroke = twoStroke;
	engineConfiguration->trigger.customTotalToothCount = teeth;
	engineConfiguration->trigger.customSkippedToothCount = 0;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
}

struct Response {
	int firstUs = -1;
	int halfUs = -1;
	int settledUs = -1;
};

Response runSpeedDrop(bool rolling, int toothPhase, int fastPhaseUs) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	engineConfiguration->idleTimingUseCycleRpm = !rolling;
	engineConfiguration->idleTimingUseRollingRpm = rolling;
	eth.smartFireRise(5);
	for (int i = 0; i < 24 * 4 + toothPhase; i++) {
		eth.smartFireRise(5);
	}

	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	idle.crankingTaperFraction = 1;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 0);
	Sensor::setMockValue(SensorType::BatteryVoltage, 13.5f);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	engine->module<IdleController>().unmock().init();

	int startUs = getTimeNowUs();
	int nextFast = startUs + fastPhaseUs;
	Response result;
	// 1000 -> 800 RPM: every 30-degree tooth now takes 6.25 ms instead of 5 ms.
	for (int i = 1; i <= 24 * 3; i++) {
		int nextTooth = startUs + i * 6250;
		while (nextFast < nextTooth) {
			eth.setTimeAndInvokeEventsUs(nextFast);
			engine->periodicFastCallback();
			float correction = engine->ignitionState.timingPidCorrection;
			int delayUs = nextFast - startUs;
			if (correction > 0.01f && result.firstUs < 0) {
				result.firstUs = delayUs;
			}
			if (correction >= 1 && result.halfUs < 0) {
				result.halfUs = delayUs;
			}
			if (correction >= 1.99f && result.settledUs < 0) {
				result.settledUs = delayUs;
			}
			nextFast += 4000;
		}
		eth.setTimeAndInvokeEventsUs(nextTooth);
		eth.firePrimaryTriggerRise();
	}
	engine->engineModules.get<IdleTargetController>().set(nullptr);
	return result;
}
} // namespace

TEST(idleTimingRolling, loadResponseDoesNotWaitForCycleBoundary) {
	for (int toothPhase : {0, 6, 12, 18}) {
		for (int fastPhaseUs : {0, 1000, 2000, 3000}) {
			SCOPED_TRACE(::testing::Message() << "tooth phase=" << toothPhase << ", fast phase=" << fastPhaseUs);
			auto full = runSpeedDrop(false, toothPhase, fastPhaseUs);
			auto rolling = runSpeedDrop(true, toothPhase, fastPhaseUs);
			EXPECT_GE(rolling.firstUs, 6250);
			EXPECT_LT(rolling.firstUs, 10250);
			EXPECT_LT(rolling.firstUs, full.firstUs);
			EXPECT_LT(rolling.halfUs, full.halfUs);
			EXPECT_LE(rolling.settledUs, full.settledUs);
			EXPECT_GE(full.firstUs, (24 - toothPhase) * 6250);
			printf("ROLLING_RESPONSE tooth=%d fast=%d first=%d/%d half=%d/%d settled=%d/%d us (full/rolling)\n",
				   toothPhase,
				   fastPhaseUs,
				   full.firstUs,
				   rolling.firstUs,
				   full.halfUs,
				   rolling.halfUs,
				   full.settledUs,
				   rolling.settledUs);
		}
	}
}

TEST(idleTimingRolling, proportionalAndDerivativeInputsFollowRollingWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	eth.smartFireRise(5);
	for (int i = 0; i < 24 * 4; i++) {
		eth.smartFireRise(5);
	}
	auto& rpm = engine->rpmCalculator;
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
	eth.smartFireRise(6.25f);
	auto rolling = rpm.getRollingCycleRpm();
	float expectedRpm = 120000.0f / 121.25f;
	float expectedRate = (expectedRpm - 1000) / 0.00625f;
	EXPECT_NEAR(rolling.rpm, expectedRpm, 0.01);
	EXPECT_NEAR(rolling.rpmRate, expectedRate, 0.1);
	EXPECT_NEAR(rpm.getCycleRpm().rpm, 1000, 0.01);

	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.dFactor = 0.003f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	auto& controller = engine->module<IdleController>().unmock();
	controller.init();
	controller.getIdlePosition(1000, 0);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(
			float(engine->ignitionState.timingPidCorrection),
			0.01f * (1000 - expectedRpm) - 0.003f * expectedRate,
			0.02);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(6.25f);
	}
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 800, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), 2, 0.02);
	engine->engineModules.get<IdleTargetController>().set(nullptr);
}

TEST(idleTimingRolling, cycleLengthAndTimestampWrap) {
	for (auto [isCam, twoStroke] : {std::pair{true, false}, std::pair{false, false}, std::pair{false, true}}) {
		SCOPED_TRACE(::testing::Message() << "cam=" << isCam << ", two stroke=" << twoStroke);
		EngineTestHelper eth(engine_type_e::TEST_ENGINE);
		configureRollingTrigger(eth, 24, isCam, twoStroke);
		// Cross the 32-bit native-tick boundary using real decoded timestamps.
		int wrapUs = (uint64_t{1} << 32) / US_TO_NT_MULTIPLIER;
		eth.setTimeAndInvokeEventsUs(wrapUs - 300000);
		for (int i = 0; i < 24 * 8; i++) {
			eth.smartFireRise(5);
		}
		EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, isCam ? 1000 : 500, 0.01);
		EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
	}
}

TEST(idleTimingRolling, stopAndTriggerChangeInvalidateHistory) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	for (int i = 0; i < 24 * 4 + 1; i++) {
		eth.smartFireRise(5);
	}
	auto& rpm = engine->rpmCalculator;
	ASSERT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	engine->OnTriggerSynchronizationLost();
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpmRate, 0);
	eth.moveTimeForwardMs(500);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(5);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	eth.smartFireRise(5);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);

	configureRollingTrigger(eth, 12);
	EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	for (int i = 0; i < 12; i++) {
		eth.smartFireRise(10);
		EXPECT_EQ(rpm.getRollingCycleRpm().rpm, 0);
	}
	for (int i = 0; i < 12 * 3; i++) {
		eth.smartFireRise(10);
	}
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpm, 1000, 0.01);
	EXPECT_NEAR(rpm.getRollingCycleRpm().rpmRate, 0, 0.01);
}

TEST(idleTimingRolling, camPhaseChangeRearmsWindow) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth, 24, false);
	for (int i = 0; i < 24 * 8 + 1; i++) {
		eth.smartFireRise(5);
	}
	auto& tc = engine->triggerCentral;
	ASSERT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 500, 0.01);
	int remainder = (tc.triggerState.getCrankSynchronizationCounter() % 2) ^ 1;
	tc.triggerState.syncEnginePhase(2, remainder, 720);
	eth.smartFireRise(5);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpm, 0);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0);
	for (int i = 0; i < 24 * 2; i++) {
		eth.smartFireRise(5);
	}
	EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 500, 0.01);
}

TEST(idleTimingRolling, missingTeethRejectRippleAtEveryDecodedEvent) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth, 36, false);
	engineConfiguration->trigger.customSkippedToothCount = 2;
	eth.setTriggerType(trigger_type_e::TT_TOOTHED_WHEEL);
	float minRpm = 10000;
	float maxRpm = 0;
	constexpr double pi = 3.14159265358979323846;
	for (int revolution = 0; revolution < 16; revolution++) {
		for (int tooth = 0; tooth < 34; tooth++) {
			double angle = revolution * 360 + tooth * 10;
			double theta = 2 * pi * angle / 720;
			int timeUs = std::lround(
					1000000 + 72000 * (angle / 720 + 0.025 * std::sin(theta) - 0.008 * std::sin(2 * theta)));
			eth.setTimeAndInvokeEventsUs(timeUs);
			eth.firePrimaryTriggerRise();
			if (revolution >= 8) {
				auto rolling = engine->rpmCalculator.getRollingCycleRpm();
				EXPECT_NEAR(rolling.rpm, 120000.0f / 72, 0.02);
				EXPECT_NEAR(rolling.rpmRate, 0, 0.05);
				float rpm = Sensor::getOrZero(SensorType::Rpm);
				minRpm = std::min(minRpm, rpm);
				maxRpm = std::max(maxRpm, rpm);
			}
		}
	}
	EXPECT_GT(maxRpm - minRpm, 300);
}

TEST(idleTimingRolling, bothEdgesWithUnequalToothWidths) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	configureRollingTrigger(eth);
	eth.setTriggerType(trigger_type_e::TT_ONE);
	ASSERT_FALSE(engine->triggerCentral.triggerShape.useOnlyRisingEdges);
	for (int i = 0; i < 16; i++) {
		eth.smartFireRise(40);
		if (i >= 4) {
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 2000, 0.01);
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
		}
		eth.smartFireFall(20);
		if (i >= 4) {
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 2000, 0.01);
			EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpmRate, 0, 0.01);
		}
	}
}

TEST(idleTimingRolling, selectionPreservesPackedCalibrationValues) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	auto readMode = [&]() {
		uint32_t word;
		std::memcpy(
				&word,
				reinterpret_cast<const uint8_t*>(engineConfiguration) + IDLE_TIMING_RPM_MODE_OFFSET,
				sizeof(word));
		return (word >> 25) & 3;
	};
	EXPECT_EQ(readMode(), 0);
	engineConfiguration->idleTimingUseCycleRpm = true;
	EXPECT_EQ(readMode(), 1);
	engineConfiguration->idleTimingUseCycleRpm = false;
	engineConfiguration->idleTimingUseRollingRpm = true;
	EXPECT_EQ(readMode(), 2);

	configureRollingTrigger(eth);
	for (int i = 0; i < 24 * 4 + 1; i++) {
		eth.smartFireRise(5);
	}
	MockIdleTargetController target;
	IIdleTargetController::Output idle;
	idle.target = {1000, 1500, 1650};
	idle.phase = IIdleController::Phase::Idling;
	EXPECT_CALL(target, getOutput(_)).WillRepeatedly(Return(idle));
	engine->engineModules.get<IdleTargetController>().set(&target);
	engineConfiguration->useIdleTimingPidControl = true;
	engineConfiguration->idleTimingPid = {};
	engineConfiguration->idleTimingPid.pFactor = 0.01f;
	engineConfiguration->idleTimingPid.dFactor = 0.003f;
	engineConfiguration->idleTimingPid.minValue = -30;
	engineConfiguration->idleTimingPid.maxValue = 30;
	auto& controller = engine->module<IdleController>().unmock();
	controller.init();
	controller.getIdlePosition(1000, 0);
	engine->rpmCalculator.rpmRate = -100;

	// An invalid fourth encoding uses legacy feedback instead of combining modes.
	auto previousConfiguration = *engineConfiguration;
	engineConfiguration->idleTimingUseCycleRpm = true;
	EXPECT_EQ(readMode(), 3);
	controller.onConfigurationChange(&previousConfiguration);
	EXPECT_EQ(engine->rpmCalculator.getRollingCycleRpm().rpm, 0);
	engine->ignitionState.updateAdvanceCorrections(50);
	EXPECT_NEAR(float(engine->ignitionState.timingPidCorrection), 0.3f, 0.02);

	// Selecting rolling again starts a fresh full-cycle window and contributes no timing while cold.
	previousConfiguration = *engineConfiguration;
	engineConfiguration->idleTimingUseCycleRpm = false;
	controller.onConfigurationChange(&previousConfiguration);
	for (int i = 0; i < 24; i++) {
		eth.smartFireRise(5);
		engine->ignitionState.updateAdvanceCorrections(50);
		EXPECT_EQ(float(engine->ignitionState.timingPidCorrection), 0);
	}
	eth.smartFireRise(5);
	EXPECT_NEAR(engine->rpmCalculator.getRollingCycleRpm().rpm, 1000, 0.01);
	engine->engineModules.get<IdleTargetController>().set(nullptr);
}
