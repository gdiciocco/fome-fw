#include "pch.h"
#include "airmass_injection_state.h"

static bool isComposite(engine_load_mode_e mode) {
	return mode == LM_SD_ALPHA_N;
}

bool AirmassInjectionState::physicallyStopped() const {
#if EFI_SHAFT_POSITION_INPUT
	auto rpm = Sensor::get(SensorType::Rpm);
	return engine->rpmCalculator.getState() == STOPPED && engine->rpmCalculator.getCachedRpm() == 0 && rpm &&
		   rpm.Value == 0 && !engine->triggerCentral.engineMovedRecently();
#else
	return false;
#endif
}

void AirmassInjectionState::publishState() const {
	engine->outputChannels.blendedStatus = static_cast<uint8_t>(m_status);
	engine->outputChannels.blendedFault = static_cast<uint8_t>(m_fault);
}

void AirmassInjectionState::latch(AirmassInjectionFault fault) {
	if (m_status != AirmassInjectionStatus::Latched) {
		m_fault = fault;
	}
	m_status = AirmassInjectionStatus::Latched;
	m_calculationAccepted = false;
	++m_epoch;
	publishState();
}

void AirmassInjectionState::observeMode(engine_load_mode_e mode) {
	if (!m_observedMode) {
		m_mode = mode;
		m_observedMode = true;
		m_status = isComposite(mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
	} else if (m_mode != mode) {
		if ((isComposite(m_mode) || isComposite(mode)) && (!physicallyStopped() || m_pendingCallbacks)) {
			latch(AirmassInjectionFault::StrategyChange);
		}
		m_mode = mode;
		++m_epoch;
		m_calculationAccepted = false;
		if (m_status != AirmassInjectionStatus::Latched) {
			m_status = isComposite(mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
		}
	}
	publishState();
}

AirmassInjectionState::CalculationToken
AirmassInjectionState::beginCalculation(engine_load_mode_e mode, float rpm, int configurationVersion) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(mode);
	++m_epoch;
	m_configurationVersion = configurationVersion;
	m_positiveRpmCalculation = std::isfinite(rpm) && rpm > 0;
	m_calculationAccepted = false;
	if (!m_positiveRpmCalculation && m_status == AirmassInjectionStatus::Ready) {
		m_status = AirmassInjectionStatus::NotReady;
	}
	publishState();
	return m_epoch;
}

void AirmassInjectionState::acceptCalculation() {
	chibios_rt::CriticalSectionLocker csl;
	m_calculationAccepted = true;
}

void AirmassInjectionState::rejectCalculation(AirmassInjectionFault fault) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	if (isComposite(m_mode) && m_positiveRpmCalculation) {
		latch(fault);
	}
}

bool AirmassInjectionState::isCalculationCurrent(CalculationToken token) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	return token == m_epoch && m_configurationVersion == engine->getGlobalConfigurationVersion();
}

void AirmassInjectionState::completeCalculation(CalculationToken token, bool publicationValid) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	if (!isComposite(m_mode) || m_status == AirmassInjectionStatus::Latched) {
		return;
	}
	auto rpm = Sensor::get(SensorType::Rpm);
	if (token != m_epoch || m_configurationVersion != engine->getGlobalConfigurationVersion() ||
		!m_positiveRpmCalculation || !rpm || !std::isfinite(rpm.Value) || rpm.Value <= 0) {
		m_status = AirmassInjectionStatus::NotReady;
		publishState();
		return;
	}
	if (!publicationValid) {
		latch(AirmassInjectionFault::Result);
		return;
	}
	m_status = m_calculationAccepted ? AirmassInjectionStatus::Ready : AirmassInjectionStatus::NotReady;
	publishState();
}

void AirmassInjectionState::onEngineStop() {
	chibios_rt::CriticalSectionLocker csl;
	++m_epoch;
	m_calculationAccepted = false;
	m_positiveRpmCalculation = false;
	if (m_status == AirmassInjectionStatus::Ready) {
		m_status = AirmassInjectionStatus::NotReady;
	}
	publishState();
}

void AirmassInjectionState::onConfigurationWrite(engine_load_mode_e proposedMode, bool strategyChanged) {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	++m_epoch;
	m_calculationAccepted = false;
	if (strategyChanged) {
		observeMode(proposedMode);
	}
	if (m_status == AirmassInjectionStatus::Ready) {
		m_status = AirmassInjectionStatus::NotReady;
	}
	publishState();
}

bool AirmassInjectionState::allowInjection() {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	return m_status == AirmassInjectionStatus::Legacy || m_status == AirmassInjectionStatus::Ready;
}

bool AirmassInjectionState::allowPrime() {
	chibios_rt::CriticalSectionLocker csl;
	observeMode(engineConfiguration->fuelAlgorithm);
	return !isComposite(m_mode) && m_status != AirmassInjectionStatus::Latched;
}

bool AirmassInjectionState::rearm() {
	chibios_rt::CriticalSectionLocker csl;
	if (!physicallyStopped() || m_pendingCallbacks != 0) {
		return false;
	}
	m_mode = engineConfiguration->fuelAlgorithm;
	m_observedMode = true;
	++m_epoch;
	m_fault = AirmassInjectionFault::None;
	m_calculationAccepted = false;
	m_positiveRpmCalculation = false;
	m_status = isComposite(m_mode) ? AirmassInjectionStatus::NotReady : AirmassInjectionStatus::Legacy;
	getFuelSchedule()->invalidate();
	publishState();
	return true;
}

AirmassInjectionStatus AirmassInjectionState::status() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_status;
}

AirmassInjectionFault AirmassInjectionState::fault() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_fault;
}

uint16_t AirmassInjectionState::pendingCallbacks() const {
	chibios_rt::CriticalSectionLocker csl;
	return m_pendingCallbacks;
}

bool AirmassInjectionState::callbacksAccepted(size_t count) {
	if (count > static_cast<size_t>(UINT16_MAX - m_pendingCallbacks)) {
		return false;
	}
	m_pendingCallbacks += count;
	return true;
}

void AirmassInjectionState::callbacksRejected(size_t count) {
	m_pendingCallbacks -= count;
}

void AirmassInjectionState::callbackCompleted() {
	chibios_rt::CriticalSectionLocker csl;
	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT, m_pendingCallbacks > 0, "untracked injection callback");
	--m_pendingCallbacks;
}

bool scheduleFuelCallbacks(const ScheduledAction* events, size_t count, bool prime) {
	chibios_rt::CriticalSectionLocker csl;
	auto& state = engine->airmassInjectionState;
	if (!(prime ? state.allowPrime() : state.allowInjection())) {
		return false;
	}
	if (!isScheduleBatchValid(events, count, getTimeNowNt())) {
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	if (!state.callbacksAccepted(count)) {
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	if (!getScheduler()->scheduleBatch(events, count)) {
		state.callbacksRejected(count);
		state.rejectCalculation(AirmassInjectionFault::Scheduling);
		return false;
	}
	return true;
}
