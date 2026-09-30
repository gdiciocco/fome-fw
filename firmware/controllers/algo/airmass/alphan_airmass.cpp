#include "pch.h"

#include "alphan_airmass.h"

namespace {
expected<float> alphaNPressure(const AirmassInputs& inputs, AlphaNPressurePolicy policy, float& coefficient) {
	coefficient = 1;
	if (policy == AlphaNPressurePolicy::EffectiveMap) {
		return inputs.EffectiveMap.Valid && std::isfinite(inputs.EffectiveMap.Map) && inputs.EffectiveMap.Map >= 0 &&
							   inputs.EffectiveMap.Map <= 1000
					 ? expected<float>(inputs.EffectiveMap.Map)
					 : expected<float>(unexpected);
	}
	if (config->alphaNBaroCompensation) {
		const float reference = config->alphaNBaroReferencePressure;
		if (!std::isfinite(reference) || reference <= 0 || !inputs.BarometricPressure ||
			!std::isfinite(inputs.BarometricPressure.Value) || inputs.BarometricPressure.Value <= 0 ||
			inputs.BarometricPressure.Value > 200) {
			coefficient = 0;
			return unexpected;
		}
		coefficient = inputs.BarometricPressure.Value / reference;
	}
	return 101.325f * coefficient;
}

bool validAlphaNInputs(const AirmassInputs& inputs) {
	return std::isfinite(inputs.Rpm) && inputs.Rpm > 0 && inputs.Tps && std::isfinite(inputs.Tps.Value) &&
		   inputs.Tps.Value >= 0 && inputs.Tps.Value <= 100 && inputs.TemperatureValid &&
		   std::isfinite(inputs.TemperatureK) && inputs.TemperatureK > 0 && std::isfinite(inputs.Displacement) &&
		   inputs.Displacement > 0 && std::isfinite(inputs.CylinderCount) && inputs.CylinderCount > 0;
}
} // namespace

AirmassResult AlphaNAirmass::getAirmass(float rpm, bool postState) {
	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation AlphaNAirmass::getAirmassForFuel(float rpm) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(true));
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation
AlphaNAirmass::evaluateAirmass(float rpm, AlphaNPressurePolicy policy, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, policy, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	return evaluateAirmass(
			rpm,
			config->alphaNMultiplyMap ? AlphaNPressurePolicy::EffectiveMap : AlphaNPressurePolicy::PureReference,
			diagnostics);
}

AirmassEvaluation
AlphaNAirmass::evaluateAirmass(float rpm, AlphaNPressurePolicy policy, const DiagnosticsTarget& diagnostics) const {
	AirmassInputs inputs;
	captureAirmassInputs(rpm, inputs);
	inputs.NativeLoad = inputs.Tps.value_or(0);
	inputs.Model = LM_ALPHA_N;
	// This wrapper explicitly requests standalone physics. Composite and dry raw
	// callers supply their pressure policy directly, independent of global mode.
	float coefficient;
	const auto pressure = alphaNPressure(inputs, policy, coefficient);
	diagnostics.map(inputs.EffectiveMap);
	diagnostics.temperature(inputs);
	diagnostics.pressure(inputs, coefficient, policy == AlphaNPressurePolicy::EffectiveMap);
	AirmassEvaluation evaluation;
	if (!validAlphaNInputs(inputs) || !pressure) {
		return evaluation;
	}
	const auto ve = evaluateVe(inputs, inputs.Tps.Value, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV, pressure.Value, inputs.TemperatureK, inputs.Displacement, inputs.CylinderCount);
	evaluation.Result = {mass, inputs.Tps.Value};
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	if (evaluation.Valid) {
		evaluation.Valid = diagnostics.consumers(inputs, mass);
	}
	if (!evaluation.Valid) {
		evaluation.Result.CylinderAirmass = 0;
	}
	return evaluation;
}

float AlphaNAirmass::getVeImpl(float rpm, percent_t load) const {
	return getDedicatedVeImpl(rpm, load);
}

float AlphaNAirmass::getDedicatedVeImpl(float rpm, float load) const {
	return interpolate3d(config->alphaNTable, config->alphaNTpsBins, load, config->alphaNRpmBins, rpm);
}

AirmassEvaluation AlphaNAirmass::evaluateRawAirmass(
		const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics, AlphaNPressurePolicy pressurePolicy) const {
	if (diagnostics) {
		*diagnostics = {};
	}
	AirmassEvaluation evaluation;
	if (!validAlphaNInputs(inputs)) {
		return evaluation;
	}
	float coefficient;
	const auto pressure = alphaNPressure(inputs, pressurePolicy, coefficient);
	if (diagnostics) {
		diagnostics->BaroCoefficient = coefficient;
	}
	if (!pressure) {
		return evaluation;
	}
	const auto ve = evaluateRawVe(inputs, inputs.Tps.Value, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV, pressure.Value, inputs.TemperatureK, inputs.Displacement, inputs.CylinderCount);
	evaluation.Result = {mass, inputs.Tps.Value};
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	return evaluation;
}
