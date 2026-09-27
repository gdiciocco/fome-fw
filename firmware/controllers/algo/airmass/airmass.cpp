#include "pch.h"

#include "airmass.h"
#include "idle_thread.h"

AirmassVeModelBase::AirmassVeModelBase(const ValueProvider3D* veTable)
	: m_veTable(veTable) {}

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
	auto evaluation = evaluateVe(rpm, load);
	if (postState) {
		publishVe(evaluation);
	}
	return evaluation.Ve * PERCENT_DIV;
}

VeEvaluation AirmassVeModelBase::evaluateVe(float rpm, float load) const {
	VeEvaluation evaluation;
	evaluateVe(rpm, load, evaluation);
	return evaluation;
}

void AirmassVeModelBase::evaluateVe(float rpm, float load, VeEvaluation& evaluation) const {
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

		evaluation.Blends[i] = result;

		// Skip extra floating point math if we can...
		if (result.Value == 0) {
			continue;
		}

		// Apply as a multiplier, not as an adder
		// Value of +5 means add 5%, aka multiply by 1.05
		ve *= ((100 + result.Value) * 0.01f);
	}

	evaluation.Ve = ve;
	evaluation.Load = load;
	evaluation.IdleLoad = idleVeLoad;
	evaluation.HasValue = true;
	evaluation.Valid = valid && std::isfinite(ve) && std::isfinite(load) && std::isfinite(idleVeLoad);
}

void AirmassVeModelBase::publishVe(const VeEvaluation& evaluation) {
	if (!evaluation.HasValue) {
		return;
	}

	for (size_t i = 0; i < efi::size(evaluation.Blends); i++) {
		const auto& result = evaluation.Blends[i];
		engine->outputChannels.veBlendParameter[i] = result.BlendParameter;
		engine->outputChannels.veBlendBias[i] = result.Bias;
		engine->outputChannels.veBlendOutput[i] = result.Value;
		engine->outputChannels.veBlendYAxis[i] = result.TableYAxis;
	}

	engine->engineState.currentVe = evaluation.Ve;
	engine->engineState.veTableYAxis = evaluation.Load;
	engine->engineState.idleVeTableYAxis = evaluation.IdleLoad;
}

void AirmassVeModelBase::publishEvaluation(const AirmassEvaluation& evaluation) {
#if EFI_TUNER_STUDIO
	if (evaluation.Map.HasValue) {
		engine->outputChannels.fallbackMap = evaluation.Map.FallbackMap;
	}
#endif
	publishVe(evaluation.Ve);
}

float AirmassVeModelBase::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
