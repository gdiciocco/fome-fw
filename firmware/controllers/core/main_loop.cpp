#include "pch.h"

#include "periodic_thread_controller.h"
#include "electronic_throttle.h"
#include "main_loop.h"
#include "perf_trace.h"

#include <algorithm>

#define MAIN_LOOP_RATE 1000

class MainLoop final : PeriodicController<1024> {
public:
	MainLoop();
	void PeriodicTask(efitick_t nowNt) override;

	void startMainLoop() {
		m_stallTimer.reset();
		startThread();
	}

private:
	template <LoopPeriod TFlag>
	LoopPeriod makePeriodFlag() const;

	LoopPeriod makePeriodFlags();

	int m_cycleCounter = 0;

	Timer m_stallTimer;
	efitick_t m_previousLoopEndNt = 0;
	efitick_t m_previousAdcStartNt = 0;
	MainLoopAdcGapDebug m_sinceLastAdc;
};

static MainLoop mainLoop CCM_OPTIONAL;
static MainLoopAdcGapDebug s_lastAdcGap;

MainLoopAdcGapDebug getMainLoopAdcGapDebug() {
	return s_lastAdcGap;
}

void initMainLoop() {
	mainLoop.startMainLoop();
}

MainLoop::MainLoop()
	: PeriodicController("MainLoop", PRIO_MAIN_LOOP, MAIN_LOOP_RATE) {}

void MainLoop::PeriodicTask(efitick_t nowNt) {
	ScopePerf perf(PE::MainLoop);
	MainLoopAdcGapDebug durations;
	bool lateAdc = false;
	if (m_previousLoopEndNt != 0) {
		m_sinceLastAdc.outsideLoopUs += NT2US(nowNt - m_previousLoopEndNt);
	}

	auto elapsedSinceLastLoop = m_stallTimer.getElapsedSecondsAndReset(nowNt);
	if (elapsedSinceLastLoop > 0.1) {
		efiPrintf("Main loop stall of %.3f sec detected", elapsedSinceLastLoop);
	}

	LoopPeriod p = makePeriodFlags();

#if HAL_USE_ADC
	if (p & ADC_UPDATE_RATE) {
		if (m_previousAdcStartNt != 0) {
			auto gapUs = NT2US(nowNt - m_previousAdcStartNt);
			if (gapUs > 10000) {
				// Snapshot every iteration since the previous ADC start. Core8
				// normally has two main-loop iterations per ADC update.
				auto count = s_lastAdcGap.count + 1;
				s_lastAdcGap = m_sinceLastAdc;
				s_lastAdcGap.count = count;
				s_lastAdcGap.gapUs = gapUs;
				if (s_lastAdcGap.previousSlowCallbackUs != 0) {
					auto modules = getSlowCallbackModuleDebug();
					s_lastAdcGap.slowModuleTotalUs = modules.totalUs;
					for (int i = 0; i < 3; i++) {
						s_lastAdcGap.slowModuleTopIndex[i] = modules.topIndex[i];
						s_lastAdcGap.slowModuleTopUs[i] = modules.topUs[i];
					}
				}
				lateAdc = true;
			}
		}
		m_previousAdcStartNt = nowNt;
		m_sinceLastAdc = {};
		auto adcStartNt = getTimeNowNt();
		updateSlowAdc(nowNt);
		durations.previousAdcUs = NT2US(getTimeNowNt() - adcStartNt);
		if (lateAdc) {
#if ENABLE_PERF_TRACE
			perfEventInstantGlobal(PE::SlowAdcGap);
			perfTraceFreezeOnSlowAdcGap();
#endif
		}
	}
#endif // HAL_USE_ADC

#if EFI_ELECTRONIC_THROTTLE_BODY
	if (p & ETB_UPDATE_RATE) {
		auto etbStartNt = getTimeNowNt();
		for (int i = 0; i < ETB_COUNT; i++) {
			auto etb = engine->etbControllers[i];

			if (etb) {
				etb->update();
			}
		}
		durations.previousEtbUs = NT2US(getTimeNowNt() - etbStartNt);
	}
#endif // EFI_ELECTRONIC_THROTTLE_BODY

	if (p & SLOW_CALLBACK_RATE) {
		auto slowStartNt = getTimeNowNt();
		doPeriodicSlowCallback();
		durations.previousSlowCallbackUs = NT2US(getTimeNowNt() - slowStartNt);
	}

	if (p & FAST_CALLBACK_RATE) {
		auto fastStartNt = getTimeNowNt();
		engine->periodicFastCallback();
		durations.previousFastCallbackUs = NT2US(getTimeNowNt() - fastStartNt);
	}
	m_previousLoopEndNt = getTimeNowNt();
	m_sinceLastAdc.previousWorkUs += NT2US(m_previousLoopEndNt - nowNt);
	m_sinceLastAdc.previousAdcUs = std::max(m_sinceLastAdc.previousAdcUs, durations.previousAdcUs);
	m_sinceLastAdc.previousEtbUs = std::max(m_sinceLastAdc.previousEtbUs, durations.previousEtbUs);
	m_sinceLastAdc.previousSlowCallbackUs =
		std::max(m_sinceLastAdc.previousSlowCallbackUs, durations.previousSlowCallbackUs);
	m_sinceLastAdc.previousFastCallbackUs =
		std::max(m_sinceLastAdc.previousFastCallbackUs, durations.previousFastCallbackUs);
}

template <LoopPeriod flag>
static constexpr int loopCounts() {
	constexpr auto hz = hzForPeriod(flag);

	// check that this cleanly divides
	static_assert(MAIN_LOOP_RATE % hz == 0);

	return MAIN_LOOP_RATE / hz;
}

template <LoopPeriod TFlag>
LoopPeriod MainLoop::makePeriodFlag() const {
	if (m_cycleCounter % loopCounts<TFlag>() == 0) {
		return TFlag;
	} else {
		return LoopPeriod::None;
	}
}

LoopPeriod MainLoop::makePeriodFlags() {
	if (m_cycleCounter >= MAIN_LOOP_RATE) {
		m_cycleCounter = 0;
	}

	LoopPeriod lp = LoopPeriod::None;
	lp |= makePeriodFlag<LoopPeriod::Period1000hz>();
	lp |= makePeriodFlag<LoopPeriod::Period500hz>();
	lp |= makePeriodFlag<LoopPeriod::Period250hz>();
	lp |= makePeriodFlag<LoopPeriod::Period20hz>();

	m_cycleCounter++;

	return lp;
}
