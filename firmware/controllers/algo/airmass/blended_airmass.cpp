#include "pch.h"

#include "blended_airmass.h"
#include "alphan_airmass.h"
#include "speed_density_airmass.h"
#if EFI_HPFP
#include "pin_repository.h"
#endif

template <typename T, size_t N>
static bool validAuthorityAxis(const T (&axis)[N], float maximum) {
	if (axis[0] < 0 || axis[N - 1] > maximum) {
		return false;
	}
	for (size_t i = 1; i < N; i++) {
		if (axis[i] <= axis[i - 1]) {
			return false;
		}
	}
	return true;
}

bool isBlendedAirmassConfigurationValid() {
	if (!isRawAirmassConfigurationValid() || engineConfiguration->useSeparateVeForIdle || !config->sdAirmassMapReady ||
		!config->alphaNAirmassMapReady || !std::isfinite(engineConfiguration->displacement) ||
		engineConfiguration->displacement <= 0 || engine->engineState.cylinderCount <= 0 ||
		!validAuthorityAxis(config->airmassBlendTpsBins, 100) ||
		!validAuthorityAxis(config->airmassBlendRpmBins, 18000)) {
		return false;
	}
	for (const auto& row : config->airmassBlendTable) {
		for (auto value : row) {
			if (value > 100) {
				return false;
			}
		}
	}
	return true;
}

static float interpolateAuthority(float tps, float rpm) {
	const auto row = priv::getBin(tps, config->airmassBlendTpsBins);
	const auto column = priv::getBin(rpm, config->airmassBlendRpmBins);
	const auto& table = config->airmassBlendTable;
	// Difference form preserves flat 0/100 stencils exactly. Weighted sums can
	// round a flat 100 above its limit or below the branch-skipping endpoint.
	const auto interpolate = [](float low, float high, float fraction) { return low + (high - low) * fraction; };
	const float left = interpolate(table[row.Idx][column.Idx], table[row.Idx + 1][column.Idx], row.Frac);
	const float right = interpolate(table[row.Idx][column.Idx + 1], table[row.Idx + 1][column.Idx + 1], row.Frac);
	return interpolate(left, right, column.Frac);
}

class BlendedAirmass::DiagnosticsTarget {
public:
	explicit DiagnosticsTarget(BlendedAirmassDiagnostics* capture)
		: m_capture(capture) {
		if (capture) {
			capture->SdMass = 0;
			capture->AlphaNMass = 0;
			capture->Sd = {};
			capture->AlphaN = {};
			capture->Map = {};
			capture->RequestedAuthority = 0;
			capture->EffectiveAuthority = 0;
			capture->Corrections.HasValue = false;
			capture->Corrections.Valid = false;
			capture->Flags = 0;
		}
	}

	explicit DiagnosticsTarget(bool postState)
		: m_postState(postState) {
		if (!postState) {
			return;
		}
		auto& output = engine->outputChannels;
		output.blendedSdMass = 0;
		output.blendedAlphaNMass = 0;
		output.blendedRequestedAuthority = 0;
		output.blendedEffectiveAuthority = 0;
		output.blendedSdLoad = 0;
		output.blendedAlphaNLoad = 0;
		output.blendedSdVe = 0;
		output.blendedAlphaNVe = 0;
		output.blendedCorrection = 0;
		output.blendedFlags = 0;
		output.fallbackMap = 0;
		// The two maps have different meanings: no single physical VE is available.
		engine->engineState.currentVe = 0;
		engine->engineState.veTableYAxis = 0;
		engine->engineState.idleVeTableYAxis = 0;
		for (size_t i = 0; i < VE_BLEND_COUNT; i++) {
			output.veBlendParameter[i] = 0;
			output.veBlendBias[i] = 0;
			output.veBlendOutput[i] = 0;
			output.veBlendYAxis[i] = 0;
		}
	}

	void map(const MapEvaluation& value) {
		if (value.HasValue) {
			m_flags |= BlendedEstimateEvaluated;
		}
		if (value.UsesEstimate) {
			m_flags |= BlendedMapEstimateUsed;
		}
		if (m_capture) {
			m_capture->Map = value;
		}
		if (m_postState) {
			engine->outputChannels.fallbackMap =
					value.HasValue && std::isfinite(value.FallbackMap) ? value.FallbackMap : 0;
		}
	}

	void authority(float value) {
		if (m_capture) {
			m_capture->RequestedAuthority = value;
			m_capture->EffectiveAuthority = value;
		}
		if (m_postState) {
			engine->outputChannels.blendedRequestedAuthority = value;
			engine->outputChannels.blendedEffectiveAuthority = value;
		}
	}

	void branch(bool sd, const AirmassEvaluation& value, const RawAirmassDiagnostics& raw) {
		m_flags |= sd ? BlendedSdEvaluated : BlendedAlphaNEvaluated;
		if (value.Valid) {
			m_flags |= sd ? BlendedSdValid : BlendedAlphaNValid;
		}
		if (m_capture) {
			(sd ? m_capture->SdMass : m_capture->AlphaNMass) = value.Result.CylinderAirmass;
			(sd ? m_capture->Sd : m_capture->AlphaN) = raw;
		}
		if (m_postState) {
			auto& output = engine->outputChannels;
			const float mass = std::isfinite(value.Result.CylinderAirmass) ? value.Result.CylinderAirmass : 0;
			const float load =
					raw.HasValue && std::isfinite(value.Result.EngineLoadPercent) ? value.Result.EngineLoadPercent : 0;
			const float tableValue = raw.HasValue && raw.Valid ? raw.TableValue : 0;
			if (sd) {
				output.blendedSdMass = mass;
				output.blendedSdLoad = load;
				output.blendedSdVe = tableValue;
			} else {
				output.blendedAlphaNMass = mass;
				output.blendedAlphaNLoad = load;
				output.blendedAlphaNVe = tableValue;
			}
		}
	}

	VeCorrectionEvaluation corrections(const AirmassInputs& inputs) {
		auto* capture = m_capture ? &m_capture->Corrections : nullptr;
		auto value =
				m_postState ? evaluateAirmassCorrectionsForFuel(inputs) : evaluateAirmassCorrections(inputs, capture);
		if (m_postState) {
			engine->outputChannels.blendedCorrection = std::isfinite(value.Multiplier) ? value.Multiplier : 0;
		}
		return value;
	}

	void finish(bool valid) {
		if (valid) {
			m_flags |= BlendedCalculationValid;
		}
		if (m_capture) {
			m_capture->Flags = m_flags;
			if (!valid) {
				m_capture->EffectiveAuthority = 0;
			}
		}
		if (m_postState) {
			engine->outputChannels.blendedFlags = m_flags;
			if (!valid) {
				engine->outputChannels.blendedEffectiveAuthority = 0;
			}
			// Actual composite table coordinates use the per-model channels.
			engine->engineState.veTableYAxis = 0;
		}
	}

private:
	BlendedAirmassDiagnostics* m_capture = nullptr;
	uint16_t m_flags = 0;
	bool m_postState = false;
};

AirmassResult BlendedAirmass::getAirmass(float rpm, bool postState) {
	DiagnosticsTarget target(postState);
	return evaluateAirmass(rpm, target).Airmass.Result;
}

BlendedAirmassEvaluation BlendedAirmass::evaluateAirmass(float rpm, BlendedAirmassDiagnostics* diagnostics) const {
	DiagnosticsTarget target(diagnostics);
	return evaluateAirmass(rpm, target);
}

BlendedAirmassEvaluation BlendedAirmass::getAirmassForFuel(float rpm) const {
	DiagnosticsTarget target(true);
	return evaluateAirmass(rpm, target);
}

BlendedAirmassEvaluation BlendedAirmass::evaluateAirmass(float rpm, DiagnosticsTarget& diagnostics) const {
	BlendedAirmassEvaluation evaluation;
	const auto fail = [&](AirmassInjectionFault fault) {
		evaluation.Fault = fault;
		diagnostics.finish(false);
		return evaluation;
	};
	if (!isBlendedAirmassConfigurationValid()) {
		return fail(AirmassInjectionFault::Configuration);
	}
	if (!std::isfinite(rpm) || rpm <= 0) {
		return fail(AirmassInjectionFault::Sensor);
	}
	AirmassInputs inputs;
	m_sd.captureInputs(rpm, inputs);
	diagnostics.map(inputs.EffectiveMap);
	// TPS selects authority even at a mass endpoint. MAP remains required by the
	// default load and common corrections, independently of either mass branch.
	if (!inputs.Tps || !std::isfinite(inputs.Tps.Value) || inputs.Tps.Value < 0 || inputs.Tps.Value > 100) {
		return fail(AirmassInjectionFault::Sensor);
	}
	const float interpolatedAuthority = interpolateAuthority(inputs.Tps.Value, inputs.Rpm);
	if (!std::isfinite(interpolatedAuthority)) {
		return fail(AirmassInjectionFault::Configuration);
	}
	// Configuration validation already bounds every cell. Contain rounding at
	// the domain edges without snapping legitimate fractional authority.
	const float authority = clampF(0, interpolatedAuthority, 100);
	diagnostics.authority(authority);
	if (inputs.EffectiveMap.HasValue && !config->mapEstimateReady) {
		return fail(AirmassInjectionFault::Configuration);
	}
	if (!inputs.EffectiveMap.Valid) {
		return fail(AirmassInjectionFault::Load);
	}
#if EFI_HPFP
	// The HPFP target-pressure table uses measured MAP independently of the
	// composite's effective MAP. An estimate cannot satisfy that dependency.
	if (engineConfiguration->hpfpCamLobes > 0 && engineConfiguration->hpfpPumpVolume > 0 &&
		isBrainPinValid(engineConfiguration->hpfpValvePin) && !resolveAirmassLoad(inputs, 0, AFR_MAP).Valid) {
		return fail(AirmassInjectionFault::Load);
	}
#endif
	float sdMass = 0;
	float alphaNMass = 0;
	if (authority < 100) {
		RawAirmassDiagnostics raw;
		auto sd = m_sd.evaluateRawAirmass(inputs, &raw);
		diagnostics.branch(true, sd, raw);
		if (!sd.Valid) {
			return fail(raw.HasValue ? AirmassInjectionFault::Result : AirmassInjectionFault::Sensor);
		}
		sdMass = sd.Result.CylinderAirmass;
	}
	if (authority > 0) {
		RawAirmassDiagnostics raw;
		auto alphaN = m_alphaN.evaluateRawAirmass(inputs, &raw);
		diagnostics.branch(false, alphaN, raw);
		if (!alphaN.Valid) {
			return fail(raw.HasValue ? AirmassInjectionFault::Result : AirmassInjectionFault::Sensor);
		}
		alphaNMass = alphaN.Result.CylinderAirmass;
	}
	// Keep exact endpoint arithmetic and skip the unused model altogether.
	const float weight = authority * 0.01f;
	const float rawMass = authority == 0   ? sdMass
						: authority == 100 ? alphaNMass
										   : (1 - weight) * sdMass + weight * alphaNMass;
	const auto correction = diagnostics.corrections(inputs);
	if (!correction.Valid) {
		return fail(AirmassInjectionFault::Correction);
	}
	const float mass = rawMass * correction.Multiplier;
	if (!std::isfinite(mass) || mass < 0) {
		return fail(AirmassInjectionFault::Result);
	}
	const auto filling = resolveAirmassLoad(inputs, mass, AFR_CylFilling);
	if (!filling.Valid) {
		return fail(AirmassInjectionFault::Result);
	}
	evaluation.NormalizedFilling = filling.Value;
	evaluation.LambdaLoad = resolveAirmassLoad(inputs, mass, inputs.LambdaOverride);
	evaluation.IgnitionLoad = resolveAirmassLoad(inputs, mass, inputs.IgnitionOverride);
	if (!evaluation.LambdaLoad.Valid || !evaluation.IgnitionLoad.Valid) {
		return fail(AirmassInjectionFault::Load);
	}
	evaluation.Airmass.Result = {mass, inputs.EffectiveMap.Map};
	evaluation.Airmass.Valid = true;
	diagnostics.finish(true);
	return evaluation;
}
