#include "pch.h"

#include "alphan_airmass.h"

AirmassResult AlphaNAirmass::getAirmass(float rpm, bool postState) {
	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation AlphaNAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	AirmassEvaluation evaluation;
	auto tps = Sensor::get(SensorType::Tps1);

	if (!tps.Valid) {
		// We are fully reliant on TPS - if it fails, there is no air-mass estimate.
		return evaluation;
	}

	// In this case, VE directly describes the cylinder filling relative to the ideal
	auto veEvaluation = evaluateVe(rpm, tps.Value, diagnostics);
	float ve = veEvaluation.Ve * PERCENT_DIV;

	// optionally use real IAT instead of fixed air temperature
	constexpr float standardIat = 20.0f; // std atmosphere temperature
	float iat = engineConfiguration->alphaNUseIat ? Sensor::get(SensorType::Iat).value_or(standardIat) : standardIat;

	float iatK = iat + 273;

	// TODO: should this be barometric pressure and/or temperature compensated?
	mass_t airmass = getAirmassImpl(
			ve,
			101.325f, // std atmosphere pressure
			iatK);

	evaluation.Result = {airmass, tps.Value};
	evaluation.Valid = veEvaluation.Valid && std::isfinite(tps.Value) && std::isfinite(iatK) && iatK > 0 &&
					   std::isfinite(airmass) && airmass >= 0;
	return evaluation;
}

float AlphaNAirmass::getVeImpl(float rpm, percent_t load) const {
	if (engineConfiguration->useDedicatedAirmassTables) {
		return getDedicatedVeImpl(rpm, load);
	}
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}

float AlphaNAirmass::getDedicatedVeImpl(float rpm, float load) const {
	return interpolate3d(config->alphaNTable, config->alphaNTpsBins, load, config->alphaNRpmBins, rpm);
}

AirmassEvaluation
AlphaNAirmass::evaluateRawAirmass(const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics) const {
	if (diagnostics) {
		diagnostics->HasValue = false;
		diagnostics->Valid = false;
	}
	AirmassEvaluation evaluation;
	if (!inputs.DedicatedTables || !inputs.ConfigurationValid || !std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 ||
		!inputs.Tps || !std::isfinite(inputs.Tps.Value) || inputs.Tps.Value < 0 || inputs.Tps.Value > 100 ||
		!std::isfinite(inputs.Displacement) || inputs.Displacement <= 0 || !std::isfinite(inputs.CylinderCount) ||
		inputs.CylinderCount <= 0 || (inputs.AlphaNUseIat && !inputs.Iat)) {
		return evaluation;
	}
	// Preserve the standalone reference and +273 temperature convention. A missing
	// requested IAT is invalid here, unlike the legacy standalone 20 C fallback.
	const float temperature = (inputs.AlphaNUseIat ? inputs.Iat.Value : 20.0f) + 273;
	if (!std::isfinite(temperature) || temperature <= 0) {
		return evaluation;
	}
	auto ve = evaluateRawVe(inputs.Rpm, inputs.Tps.Value, diagnostics);
	const float mass =
			getAirmassImpl(ve.Ve * PERCENT_DIV, 101.325f, temperature, inputs.Displacement, inputs.CylinderCount);
	evaluation.Result = {mass, inputs.Tps.Value};
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	return evaluation;
}
