#include "pch.h"

#include "airmass.h"
#include "idle_thread.h"
#include "gppwm_channel.h"
#include "speed_density_base.h"

AirmassVeModelBase::AirmassVeModelBase(const ValueProvider3D* veTable)
	: m_veTable(veTable) {}

template <typename T, size_t N>
static bool isAxisValid(const T (&axis)[N], float maximum, float minimum = 0) {
	// Packed integer storage guarantees finite values. With a strictly ascending
	// axis, checking the first and last bins bounds the whole axis.
	if (axis[0] < minimum || axis[N - 1] > maximum) {
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

bool isMapEstimateAxesValid() {
	return isAxisValid(config->mapEstimateTpsBins, 100) && isAxisValid(config->mapEstimateRpmBins, 18000);
}

bool isMapEstimateConfigurationValid() {
	if (!isMapEstimateAxesValid()) {
		return false;
	}
	for (const auto& row : config->mapEstimateTable) {
		for (float value : row) {
			if (!std::isfinite(value) || value < 0 || value > 600) {
				return false;
			}
		}
	}
	return true;
}

bool isRawAirmassConfigurationValid() {
	return engineConfiguration->useDedicatedAirmassTables && isAirmassConfigurationValid() &&
		   isAxisValid(config->veLoadBins, 1000) && isAxisValid(config->veRpmBins, 18000);
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

		// Apply as a multiplier, not as an adder
		// Value of +5 means add 5%, aka multiply by 1.05
		ve = applyVeCorrection(ve, result.Value);
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

float AirmassVeModelBase::getDedicatedVeImpl(float rpm, float load) const {
	return getVeImpl(rpm, load);
}

VeEvaluation AirmassVeModelBase::evaluateRawVe(float rpm, float load, RawAirmassDiagnostics* diagnostics) const {
	const float value = m_veTable ? m_veTable->getValue(rpm, load) : getDedicatedVeImpl(rpm, load);
	VeEvaluation evaluation{value, std::isfinite(value) && value >= 0};
	if (diagnostics) {
		diagnostics->TableValue = value;
		diagnostics->HasValue = true;
		diagnostics->Valid = evaluation.Valid;
	}
	return evaluation;
}

AirmassLoad resolveAirmassLoad(const AirmassInputs& inputs, mass_t finalMass, load_override_e selector) {
	AirmassLoad result;
	expected<float> sensor = unexpected;
	float maximum = 100;
	switch (selector) {
		case AFR_None:
			result.Value = inputs.EffectiveMap.Map;
			result.Source = AirmassLoadSource::EffectiveMap;
			result.Unit = AirmassLoadUnit::Kpa;
			result.Valid = inputs.EffectiveMap.Valid && std::isfinite(result.Value) && result.Value >= 0 &&
						   result.Value <= 1000;
			result.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
			return result;
		case AFR_MAP:
			sensor = inputs.MeasuredMap;
			result.Source = AirmassLoadSource::MeasuredMap;
			result.Unit = AirmassLoadUnit::Kpa;
			maximum = 1000;
			break;
		case AFR_Tps:
			sensor = inputs.Tps;
			result.Source = AirmassLoadSource::Tps;
			break;
		case AFR_AccPedal:
			sensor = inputs.Pedal;
			result.Source = AirmassLoadSource::Pedal;
			break;
		case AFR_CylFilling: {
			result.Source = AirmassLoadSource::CylinderFilling;
			if (!std::isfinite(finalMass) || finalMass < 0 || !std::isfinite(inputs.Displacement) ||
				inputs.Displacement <= 0 || !std::isfinite(inputs.CylinderCount) || inputs.CylinderCount <= 0) {
				return result;
			}
			const float standardCharge = idealGasLaw(inputs.Displacement / inputs.CylinderCount, 101.325f, 293.15f);
			result.Value = 100 * finalMass / standardCharge;
			result.Valid = std::isfinite(result.Value) && result.Value >= 0;
			return result;
		}
		default:
			return result;
	}
	result.Value = sensor.value_or(0);
	result.Valid = sensor.Valid && std::isfinite(result.Value) && result.Value >= 0 && result.Value <= maximum;
	return result;
}

namespace {
expected<float> boundedInput(expected<float> value, float maximum) {
	return value.Valid && std::isfinite(value.Value) && value.Value >= 0 && value.Value <= maximum
				 ? value
				 : expected<float>(unexpected);
}

// At most two input channels per correction. This local cache exists only for the
// composite correction pass, never for standalone model evaluation. NaN encodes
// an invalid captured channel; all valid correction inputs must be finite.
class CorrectionChannels {
public:
	explicit CorrectionChannels(const AirmassInputs& inputs)
		: m_inputs(inputs) {}

	expected<float> read(gppwm_channel_e channel) {
		switch (channel) {
			case GPPWM_Zero:
				return 0;
			case GPPWM_Rpm:
				return m_inputs.Rpm;
			case GPPWM_Tps:
				return boundedInput(m_inputs.Tps, 100);
			case GPPWM_Map:
				return boundedInput(m_inputs.MeasuredMap, 1000);
			case GPPWM_Iat:
				return m_inputs.Iat;
			case GPPWM_AccelPedal:
				return boundedInput(m_inputs.Pedal, 100);
			case GPPWM_FuelLoad:
				return m_inputs.PreviousFuelingLoad;
			case GPPWM_IgnLoad:
				return m_inputs.PreviousIgnitionLoad;
			default:
				break;
		}
		for (size_t i = 0; i < m_count; i++) {
			if (m_channels[i] == channel) {
				return std::isfinite(m_values[i]) ? expected<float>(m_values[i]) : expected<float>(unexpected);
			}
		}
		// The legacy GPPWM vehicle-speed reader substitutes zero on failure. The
		// strict path needs the sensor's validity, just like other sensor channels.
		// Derived EGT/GPPWM outputs expose no source validity through this API:
		// their contract is a finite sampled value, not proven sensor health.
		auto value = channel == GPPWM_VehicleSpeed ? Sensor::get(SensorType::VehicleSpeed) : readGppwmChannel(channel);
		m_channels[m_count] = channel;
		m_values[m_count++] = value.value_or(NAN);
		return value;
	}

private:
	const AirmassInputs& m_inputs;
	float m_values[VE_BLEND_COUNT * 2];
	gppwm_channel_e m_channels[VE_BLEND_COUNT * 2];
	size_t m_count = 0;
};
} // namespace

VeCorrectionEvaluation evaluateAirmassCorrections(const AirmassInputs& inputs, VeCorrectionDiagnostics* diagnostics) {
	if (diagnostics) {
		diagnostics->HasValue = false;
		diagnostics->Valid = false;
	}
	VeCorrectionEvaluation evaluation{1, true};
	CorrectionChannels channels(inputs);
	for (size_t i = 0; i < efi::size(config->veBlends); i++) {
		const auto& cfg = config->veBlends[i];
		BlendResult result{};
		if (cfg.blendParameter != GPPWM_Zero) {
			auto parameter = channels.read(cfg.blendParameter);
			auto load = cfg.yAxisOverride == GPPWM_Zero
							  ? (inputs.EffectiveMap.Valid ? boundedInput(inputs.EffectiveMap.Map, 1000)
														   : expected<float>(unexpected))
							  : channels.read(cfg.yAxisOverride);
			if (!parameter || !load || !std::isfinite(parameter.Value) || !std::isfinite(load.Value) ||
				!std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 || !isAxisValid(cfg.loadBins, 1000) ||
				!isAxisValid(cfg.rpmBins, 18000) || !isAxisValid(cfg.blendBins, 1000, -1000)) {
				evaluation.Valid = false;
			} else {
				result = calculateBlend(cfg, inputs.Rpm, load.Value, parameter.Value);
				const float factor = applyVeCorrection(1, result.Value);
				evaluation.Valid =
						evaluation.Valid && std::isfinite(result.Value) && std::isfinite(factor) && factor >= 0;
				evaluation.Multiplier = applyVeCorrection(evaluation.Multiplier, result.Value);
			}
		}
		if (diagnostics) {
			diagnostics->Blends[i] = result;
		}
	}
	evaluation.Valid = evaluation.Valid && std::isfinite(evaluation.Multiplier) && evaluation.Multiplier >= 0;
	if (diagnostics) {
		diagnostics->HasValue = true;
		diagnostics->Valid = evaluation.Valid;
	}
	return evaluation;
}
