#include "pch.h"
#include "airmass_injection_state.h"

namespace {
using Status = AirmassInjectionStatus;
using Fault = AirmassInjectionFault;

AirmassInjectionState& gate() {
	return engine->airmassInjectionState;
}

void selectComposite() {
	Sensor::setMockValue(SensorType::Rpm, 0);
	gate().onConfigurationWrite(LM_SD_ALPHA_N, engineConfiguration->fuelAlgorithm != LM_SD_ALPHA_N);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
}

AirmassInjectionState::CalculationToken beginPositiveCalculation() {
	Sensor::setMockValue(SensorType::Rpm, 1000);
	return gate().beginCalculation(engineConfiguration->fuelAlgorithm, 1000, engine->getGlobalConfigurationVersion());
}

void makeReady() {
	auto token = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().completeCalculation(token, true);
	ASSERT_EQ(Status::Ready, gate().status());
}

bool queuePulse(int startUs, int endUs, InjectorContext ctx = {}) {
	ctx.outputsMask = 1;
	ScheduledAction events[] = {
			{getTimeNowNt() + US2NT(startUs), {scheduledStartInjection, ctx}},
			{getTimeNowNt() + US2NT(endUs), {scheduledEndInjection, ctx}},
	};
	return scheduleFuelCallbacks(events, efi::size(events));
}
} // namespace

TEST(airmassInjectionGate, StartupRequiresPositiveRpmAndCompletePublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto token = gate().beginCalculation(LM_SD_ALPHA_N, 0, engine->getGlobalConfigurationVersion());
	gate().rejectCalculation(Fault::Sensor);
	gate().acceptCalculation();
	gate().completeCalculation(token, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_EQ(Fault::None, gate().fault());
	EXPECT_FALSE(gate().allowInjection());

	token = beginPositiveCalculation();
	gate().acceptCalculation();
	EXPECT_FALSE(gate().allowInjection());
	gate().completeCalculation(token, true);
	EXPECT_TRUE(gate().allowInjection());
	EXPECT_EQ(static_cast<uint8_t>(Status::Ready), engine->outputChannels.blendedStatus);
	EXPECT_FALSE(gate().allowPrime());
}

TEST(airmassInjectionGate, FaultSurvivesStrategyChangesAndValidCalculations) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	gate().rejectCalculation(Fault::Sensor);
	EXPECT_FALSE(getLimpManager()->allowInjection().value);
	EXPECT_EQ(ClearReason::Airmass, getLimpManager()->allowInjection().reason);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_FALSE(gate().allowPrime());
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	auto token = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().completeCalculation(token, true);
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_EQ(Fault::Sensor, gate().fault());
	EXPECT_FALSE(gate().rearm());
}

TEST(airmassInjectionGate, StopAndConfigWriteInvalidateOldPublication) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	auto old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onEngineStop();
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_EQ(Fault::None, gate().fault());
	makeReady();

	old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onConfigurationWrite(LM_SD_ALPHA_N, false);
	EXPECT_FALSE(gate().isCalculationCurrent(old));
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	makeReady();
}

TEST(airmassInjectionGate, StrategyChangedAwayAndBackCannotReuseToken) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	auto old = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().onConfigurationWrite(LM_SPEED_DENSITY, true);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	gate().onConfigurationWrite(LM_SD_ALPHA_N, true);
	engineConfiguration->fuelAlgorithm = LM_SD_ALPHA_N;
	gate().completeCalculation(old, true);
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_EQ(Fault::StrategyChange, gate().fault());
}

TEST(airmassInjectionGate, InvalidFinalPublicationLatchesAndNanRpmCannotUnlock) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	auto token = beginPositiveCalculation();
	gate().acceptCalculation();
	Sensor::setMockValue(SensorType::Rpm, NAN);
	gate().completeCalculation(token, true);
	EXPECT_EQ(Status::NotReady, gate().status());
	token = beginPositiveCalculation();
	gate().acceptCalculation();
	gate().completeCalculation(token, false);
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_EQ(Fault::Result, gate().fault());
}

TEST(airmassInjectionGate, AcceptedLegacyPulseDrainsBeforeStoppedRearm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	ASSERT_TRUE(queuePulse(1000, 3000));
	EXPECT_EQ(2, gate().pendingCallbacks());
	selectComposite();
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_FALSE(gate().rearm());

	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(1, gate().pendingCallbacks());
	EXPECT_FALSE(gate().rearm());
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
	ASSERT_TRUE(gate().rearm());
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_EQ(0, engine->scheduler.size());
	makeReady();
}

TEST(airmassInjectionGate, OverlappingPrimaryAndStage2CallbacksRemainBalancedAfterFault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	InjectorContext ctx;
	ctx.outputsMask = 1;
	ctx.stage2Active = true;
	ScheduledAction staged[] = {
			{getTimeNowNt() + US2NT(1000), {scheduledStartInjection, ctx}},
			{getTimeNowNt() + US2NT(2500), {scheduledEndInjectionStage2, ctx}},
			{getTimeNowNt() + US2NT(3000), {scheduledEndInjection, ctx}},
	};
	ASSERT_TRUE(scheduleFuelCallbacks(staged, efi::size(staged)));
	ASSERT_TRUE(queuePulse(2000, 4000));
	gate().rejectCalculation(Fault::Load);
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(2, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(1, enginePins.injectorsStage2[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, enginePins.injectorsStage2[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
}

TEST(airmassInjectionGate, FaultPreventsNewSplitButQueuedSplitDrains) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	InjectorContext ctx;
	ctx.splitDurationUs = 1000;
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	gate().rejectCalculation(Fault::Sensor);
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	EXPECT_EQ(0, engine->scheduler.size());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());

	Sensor::setMockValue(SensorType::Rpm, 0);
	ASSERT_TRUE(gate().rearm());
	makeReady();
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	eth.moveTimeForwardAndInvokeEventsUs(2000);
	ASSERT_EQ(2, gate().pendingCallbacks()); // Second half already accepted.
	gate().rejectCalculation(Fault::Sensor);
	eth.moveTimeForwardAndInvokeEventsUs(3000);
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
}

TEST(airmassInjectionGate, DelayedLegacyPrimeIsSuppressedAtActualStart) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->primingDelay = 0;
	engine->module<PrimeController>()->onIgnitionStateChanged(true);
	ASSERT_EQ(1, gate().pendingCallbacks());
	selectComposite();
	EXPECT_FALSE(gate().rearm());
	eth.moveTimeForwardAndInvokeEventsUs(100000);
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	ASSERT_TRUE(gate().rearm());
	EXPECT_FALSE(engine->module<PrimeController>()->isPriming());
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(airmassInjectionGate, ActivePrimeClosesCapturedMaskAfterConfigurationChange) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	MockInjectorModel2 injector;
	engine->module<InjectorModelPrimary>().set(&injector);
	EXPECT_CALL(injector, getInjectionDuration(testing::_)).WillOnce(Return(20.0f));
	engine->engineState.cylinderCount = 4;
	engine->module<PrimeController>()->onPrimeStart();
	eth.executeActions();
	ASSERT_EQ(1, enginePins.injectors[3].getOverlappingCounter());
	engine->engineState.cylinderCount = 2;
	selectComposite();
	eth.moveTimeForwardAndInvokeEventsUs(20000);
	EXPECT_EQ(0, enginePins.injectors[3].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_FALSE(engine->module<PrimeController>()->isPriming());
}

TEST(airmassInjectionGate, SpinningUpCannotRearmEvenWithZeroSensorRpm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	eth.setTriggerType(trigger_type_e::TT_ONE);
	engineConfiguration->isFasterEngineSpinUpEnabled = true;
	selectComposite();
	makeReady();
	gate().rejectCalculation(Fault::Sensor);
	Sensor::setMockValue(SensorType::Rpm, 0);
	eth.fireRise(1);
	ASSERT_EQ(SPINNING_UP, engine->rpmCalculator.getState());
	ASSERT_EQ(0, engine->rpmCalculator.getCachedRpm());
	ASSERT_TRUE(engine->rpmCalculator.isStopped()); // Engine-math predicate, not physical stop.
	ASSERT_TRUE(engine->triggerCentral.engineMovedRecently());

	engine->periodicSlowCallback();
	EXPECT_EQ(SPINNING_UP, engine->rpmCalculator.getState());
	EXPECT_FALSE(gate().rearm());

	advanceTimeUs(10e6);
	ASSERT_FALSE(engine->triggerCentral.engineMovedRecently());
	EXPECT_FALSE(gate().rearm()); // Timeout must still go through the stop transition.
	engine->periodicSlowCallback();
	EXPECT_EQ(STOPPED, engine->rpmCalculator.getState());
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_EQ(Fault::Sensor, gate().fault());
	EXPECT_TRUE(gate().rearm());
	EXPECT_EQ(Status::NotReady, gate().status());
	EXPECT_FALSE(gate().allowInjection());
}

TEST(airmassInjectionGate, AccountingPrecedesImmediateCallbacks) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	class ImmediateExecutor : public Scheduler {
	public:
		void schedule(const char*, scheduling_s*, efitick_t, action_s) override {
			ADD_FAILURE() << "fuel must use atomic batch admission";
		}
		void cancel(scheduling_s*) override {}
		bool scheduleBatch(const ScheduledAction* events, size_t count) override {
			EXPECT_EQ(2u, count);
			EXPECT_EQ(2, gate().pendingCallbacks());
			auto open = events[0].action;
			open.execute();
			EXPECT_EQ(1, gate().pendingCallbacks());
			auto close = events[1].action;
			close.execute();
			EXPECT_EQ(0, gate().pendingCallbacks());
			return true;
		}
	} executor;
	engine->scheduler.setMockExecutor(&executor);
	ASSERT_TRUE(queuePulse(0, 1000));
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	engine->scheduler.setMockExecutor(nullptr);
}

TEST(airmassInjectionGate, ExhaustedPoolAdmitsNoPulseAndRollsBackAccounting) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	selectComposite();
	makeReady();
	for (size_t i = 0; i < 64; i++) {
		getScheduler()->schedule("fill", nullptr, getTimeNowNt() + US2NT(10000), {+[](void*) {}, nullptr});
	}
	ASSERT_EQ(64, engine->scheduler.size());
	EXPECT_FALSE(queuePulse(0, 1000));
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(Status::Latched, gate().status());
	EXPECT_EQ(Fault::Scheduling, gate().fault());
	eth.moveTimeForwardAndInvokeEventsUs(10000);
}

TEST(airmassInjectionGate, StandaloneModelsRequireValidPublicationAndRecoverWithoutRearm) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	for (auto mode : {LM_SPEED_DENSITY, LM_ALPHA_N, LM_REAL_MAF}) {
		SCOPED_TRACE(static_cast<int>(mode));
		engineConfiguration->fuelAlgorithm = mode;
		EXPECT_FALSE(gate().allowInjection());
		EXPECT_TRUE(gate().allowPrime());
		auto token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		ASSERT_TRUE(gate().allowInjection());
		ASSERT_EQ(Status::Legacy, gate().status());

		token = beginPositiveCalculation();
		EXPECT_FALSE(gate().allowInjection());
		engine->engineState.airmassCalculationValid = false;
		gate().completeCalculation(token, true);
		EXPECT_FALSE(gate().allowInjection());
		EXPECT_TRUE(gate().allowPrime());
		EXPECT_EQ(Status::Legacy, gate().status());
		EXPECT_EQ(Fault::None, gate().fault());

		token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, false);
		EXPECT_FALSE(gate().allowInjection());

		token = beginPositiveCalculation();
		engine->engineState.airmassCalculationValid = true;
		gate().completeCalculation(token, true);
		EXPECT_TRUE(gate().allowInjection());
	}
}

TEST(airmassInjectionGate, StandaloneRejectDrainsAcceptedPulseAndBlocksWallFuelAndNewSplit) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_SPEED_DENSITY;
	auto token = beginPositiveCalculation();
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(token, true);
	InjectorContext ctx;
	ctx.splitDurationUs = 1000;
	ASSERT_TRUE(queuePulse(1000, 2000, ctx));
	gate().rejectCalculation(Fault::Load);
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_FALSE(queuePulse(1000, 3000));
	EXPECT_EQ(Status::Legacy, gate().status());

	// Even stale positive cylinder fuel and a wall-film state cannot get as far
	// as duration calculation once the standalone admission gate has closed.
	engine->cylinders[0].setInjectionMass(1);
	testing::StrictMock<MockInjectorModel2> injector;
	engine->module<InjectorModelPrimary>().set(&injector);
	InjectionEvent event;
	event.injectionStartAngle = 90;
	event.onTriggerTooth({getTimeNowNt(), 0, 180, 0, 180});
	EXPECT_EQ(2, gate().pendingCallbacks());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(1, enginePins.injectors[0].getOverlappingCounter());
	eth.moveTimeForwardAndInvokeEventsUs(1000);
	EXPECT_EQ(0, enginePins.injectors[0].getOverlappingCounter());
	EXPECT_EQ(0, gate().pendingCallbacks());
	EXPECT_EQ(0, engine->scheduler.size());
}

TEST(airmassInjectionGate, StandaloneStopAndTuneWriteRejectStaleCompletion) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	auto old = beginPositiveCalculation();
	gate().onConfigurationWrite(LM_ALPHA_N, false);
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(old, true);
	EXPECT_FALSE(gate().allowInjection());
	auto current = beginPositiveCalculation();
	engine->engineState.airmassCalculationValid = true;
	gate().completeCalculation(current, true);
	EXPECT_TRUE(gate().allowInjection());
	gate().completeCalculation(old, false);
	EXPECT_TRUE(gate().allowInjection());

	gate().onEngineStop();
	EXPECT_FALSE(gate().allowInjection());
	EXPECT_TRUE(gate().allowPrime());
	gate().completeCalculation(current, true);
	EXPECT_FALSE(gate().allowInjection());
}
