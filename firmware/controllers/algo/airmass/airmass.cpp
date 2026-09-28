#include "pch.h"

#include "airmass.h"
#include "idle_thread.h"

AirmassVeModelBase::AirmassVeModelBase(const ValueProvider3D* veTable)
	: m_veTable(veTable) {}

template <typename T, size_t N>
static bool isAxisValid(const T (&axis)[N], float maximum) {
	// Unsigned packed storage guarantees finite, nonnegative values. With a
	// strictly ascending axis, checking the last bin bounds the whole axis.
	if (axis[N - 1] > maximum) {
		return false;
	}
	for (size_t i = 1; i < N; i++) {
		if (axis[i] <= axis[i - 1]) {
			return false;
		}
	}
	return true;
}

static const char* getAirmassConfigurationError() {
	if (!engineConfiguration->useDedicatedAirmassTables) {
		return nullptr;
	}
	if (engineConfiguration->veOverrideMode != VE_None) {
		return "Dedicated airmass tables require VE load override None";
	}
	if (!isAxisValid(config->alphaNTpsBins, 100)) {
		return "Alpha-N TPS axis must be ascending in 0..100 percent";
	}
	if (!isAxisValid(config->alphaNRpmBins, 18000)) {
		return "Alpha-N RPM axis must be ascending in 0..18000 RPM";
	}
	if (!isAxisValid(config->mafLoadBins, 1000)) {
		return "MAF load axis must be ascending in 0..1000 percent";
	}
	if (!isAxisValid(config->mafRpmBins, 18000)) {
		return "MAF RPM axis must be ascending in 0..18000 RPM";
	}
	return nullptr;
}

bool isAirmassConfigurationValid() {
	return getAirmassConfigurationError() == nullptr;
}

bool validateAirmassConfiguration() {
	if (const auto* error = getAirmassConfigurationError()) {
		firmwareError(ObdCode::CUSTOM_ERR_ASSERT, "%s", error);
		return false;
	}
	return true;
}

static float getVeLoadAxis(ve_override_e mode, float passedLoad, bool& valid) {
	switch (mode) {
		case VE_None:
			return passedLoad;
		case VE_MAP:
		case VE_TPS: {
			auto value = Sensor::get(mode == VE_MAP ? SensorType::Map : SensorType::Tps1);
			valid = valid && value.Valid;
			return value.value_or(0);
		}
		default:
			valid = false;
			return 0;
	}
}

float AirmassVeModelBase::getVe(float rpm, float load, bool postState) const {
	auto evaluation = evaluateVe(rpm, load, DiagnosticsTarget(postState));
	return evaluation.Ve * PERCENT_DIV;
}

VeEvaluation AirmassVeModelBase::evaluateVe(float rpm, float load, VeDiagnostics* diagnostics) const {
	return evaluateVe(rpm, load, DiagnosticsTarget(diagnostics));
}

VeEvaluation AirmassVeModelBase::evaluateVe(float rpm, float load, const DiagnosticsTarget& diagnostics) const {
	// Check live edits before interpolation without changing fault state in a dry
	// query. The live fuel owner, boot and burn paths report configuration errors.
	if (!isAirmassConfigurationValid()) {
		return {};
	}

	bool valid = std::isfinite(rpm);
	// Override the load value if necessary
	load = getVeLoadAxis(engineConfiguration->veOverrideMode, load, valid);

	percent_t ve = m_veTable ? m_veTable->getValue(rpm, load) : getVeImpl(rpm, load);

	float idleVeLoad = load;

#if EFI_IDLE_CONTROL
	auto tps = Sensor::get(SensorType::DriverThrottleIntent);
	// get VE from the separate table for Idle if idling
	if (engine->module<IdleController>()->isIdlingOrTaper() && tps && engineConfiguration->useSeparateVeForIdle) {
		idleVeLoad = getVeLoadAxis(engineConfiguration->idleVeOverrideMode, load, valid);

		percent_t idleVe =
				interpolate3d(config->idleVeTable, config->idleVeLoadBins, idleVeLoad, config->idleVeRpmBins, rpm);

		// interpolate between idle table and normal (running) table using TPS threshold
		// 0 TPS -> idle table
		// 1/2 threshold -> idle table
		// idle threshold -> normal table
		float idleThreshold = engineConfiguration->idlePidDeactivationTpsThreshold;
		ve = interpolateClamped(idleThreshold / 2, idleVe, idleThreshold, ve, tps.Value);
	}
#endif // EFI_IDLE_CONTROL

	// Add any adjustments if configured
	for (size_t i = 0; i < efi::size(config->veBlends); i++) {
		auto result = calculateBlend(config->veBlends[i], rpm, load);

		diagnostics.blend(i, result);

		// Skip extra floating point math if we can...
		if (result.Value == 0) {
			continue;
		}

		// Apply as a multiplier, not as an adder
		// Value of +5 means add 5%, aka multiply by 1.05
		ve *= ((100 + result.Value) * 0.01f);
	}

	VeEvaluation evaluation{ve, valid && std::isfinite(ve) && std::isfinite(load) && std::isfinite(idleVeLoad)};
	diagnostics.ve(evaluation, load, idleVeLoad);
	return evaluation;
}

static void publishBlend(size_t index, const BlendResult& result) {
	engine->outputChannels.veBlendParameter[index] = result.BlendParameter;
	engine->outputChannels.veBlendBias[index] = result.Bias;
	engine->outputChannels.veBlendOutput[index] = result.Value;
	engine->outputChannels.veBlendYAxis[index] = result.TableYAxis;
}

static void publishVeValues(percent_t ve, float load, float idleLoad) {
	engine->engineState.currentVe = ve;
	engine->engineState.veTableYAxis = load;
	engine->engineState.idleVeTableYAxis = idleLoad;
}

static void publishMap(const MapEvaluation& result) {
#if EFI_TUNER_STUDIO
	if (result.HasValue) {
		engine->outputChannels.fallbackMap = result.FallbackMap;
	}
#else
	(void)result;
#endif
}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(bool postState)
	: m_postState(postState) {}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(VeDiagnostics* diagnostics)
	: m_ve(diagnostics) {
	if (m_ve) {
		m_ve->HasValue = false;
		m_ve->Valid = false;
	}
}

AirmassVeModelBase::DiagnosticsTarget::DiagnosticsTarget(AirmassDiagnostics* diagnostics)
	: DiagnosticsTarget(diagnostics ? &diagnostics->Ve : nullptr) {
	if (diagnostics) {
		m_map = &diagnostics->Map;
		m_map->HasValue = false;
		m_map->Valid = false;
	}
}

void AirmassVeModelBase::DiagnosticsTarget::blend(size_t index, const BlendResult& result) const {
	if (m_ve) {
		m_ve->Blends[index] = result;
	}
	if (m_postState) {
		publishBlend(index, result);
	}
}

void AirmassVeModelBase::DiagnosticsTarget::ve(const VeEvaluation& result, float load, float idleLoad) const {
	if (m_ve) {
		m_ve->Ve = result.Ve;
		m_ve->Load = load;
		m_ve->IdleLoad = idleLoad;
		m_ve->Valid = result.Valid;
		m_ve->HasValue = true;
	}
	if (m_postState) {
		publishVeValues(result.Ve, load, idleLoad);
	}
}

void AirmassVeModelBase::DiagnosticsTarget::map(const MapEvaluation& result) const {
	if (m_map) {
		*m_map = result;
	}
	if (m_postState) {
		publishMap(result);
	}
}

void AirmassVeModelBase::publishVe(const VeDiagnostics& diagnostics) {
	if (!diagnostics.HasValue) {
		return;
	}

	for (size_t i = 0; i < efi::size(diagnostics.Blends); i++) {
		publishBlend(i, diagnostics.Blends[i]);
	}

	publishVeValues(diagnostics.Ve, diagnostics.Load, diagnostics.IdleLoad);
}

void AirmassVeModelBase::publishEvaluation(const AirmassDiagnostics& diagnostics) {
	publishMap(diagnostics.Map);
	publishVe(diagnostics.Ve);
}

float AirmassVeModelBase::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
