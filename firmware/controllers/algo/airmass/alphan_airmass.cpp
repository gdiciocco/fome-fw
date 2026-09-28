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
		return interpolate3d(config->alphaNTable, config->alphaNTpsBins, load, config->alphaNRpmBins, rpm);
	}
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
