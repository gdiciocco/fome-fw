#include "pch.h"
#include "speed_density_airmass.h"

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, bool postState) {
	ScopePerf perf(PE::GetSpeedDensityFuel);

	return evaluateAirmass(rpm, DiagnosticsTarget(postState)).Result;
}

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, float map, bool postState) {
	return evaluateAirmass(rpm, map, DiagnosticsTarget(postState)).Result;
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics) const {
	return evaluateAirmass(rpm, DiagnosticsTarget(diagnostics));
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const {
	auto map = evaluateMap(rpm);
	diagnostics.map(map);
	auto evaluation = evaluateAirmass(rpm, map.Map, diagnostics);
	evaluation.Valid = evaluation.Valid && map.Valid;
	return evaluation;
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, float map, AirmassDiagnostics* diagnostics) const {
	DiagnosticsTarget target(diagnostics);
	MapEvaluation mapEvaluation;
	mapEvaluation.Map = map;
	mapEvaluation.Valid = std::isfinite(map) && map >= 0;
	// Retain the supplied numerical MAP without marking fallback diagnostics present.
	target.map(mapEvaluation);
	return evaluateAirmass(rpm, map, target);
}

AirmassEvaluation
SpeedDensityAirmass::evaluateAirmass(float rpm, float map, const DiagnosticsTarget& diagnostics) const {
	AirmassEvaluation evaluation;
	// An explicit MAP does not calculate or publish fallback MAP diagnostics.
	/**
	 * most of the values are pre-calculated for performance reasons
	 */
	float tChargeK = engine->engineState.sd.tChargeK;
	if (std::isnan(tChargeK)) {
		warning(ObdCode::CUSTOM_ERR_TCHARGE_NOT_READY2,
				"tChargeK not ready"); // this would happen before we have CLT reading for example
		return evaluation;
	}

	auto veEvaluation = evaluateVe(rpm, map, diagnostics);
	float ve = veEvaluation.Ve * PERCENT_DIV;

	float airMass = getAirmassImpl(ve, map, tChargeK);
	if (std::isnan(airMass)) {
		warning(ObdCode::CUSTOM_ERR_6685, "NaN airMass");
		return evaluation;
	}

	evaluation.Result = {
			airMass,
			map, // AFR/VE table Y axis
	};
	evaluation.Valid = std::isfinite(map) && map >= 0 && veEvaluation.Valid && std::isfinite(tChargeK) &&
					   tChargeK > 0 && std::isfinite(airMass) && airMass >= 0;
	return evaluation;
}

float SpeedDensityAirmass::getAirflow(float rpm, float map, bool postState) {
	auto airmassResult = getAirmass(rpm, map, postState);

	float massPerCycle = airmassResult.CylinderAirmass * engine->engineState.cylinderCount;

	if (!engineConfiguration->twoStroke) {
		// 4 stroke engines only do a half cycle per rev
		massPerCycle = massPerCycle / 2;
	}

	// g/s
	return massPerCycle * rpm / 60;
}

float SpeedDensityAirmass::getMap(float rpm, bool postState) const {
	auto evaluation = evaluateMap(rpm);
	DiagnosticsTarget(postState).map(evaluation);
	return evaluation.Map;
}

MapEvaluation SpeedDensityAirmass::evaluateMap(float rpm) const {
	auto tps = Sensor::get(SensorType::Tps1);
	float fallbackMap = m_mapEstimationTable->getValue(rpm, tps.value_or(0));
	MapEvaluation evaluation;
	evaluation.FallbackMap = fallbackMap;
	evaluation.HasValue = true;

	auto map = Sensor::get(SensorType::Map);
	if (!map) {
		// Preserve the legacy estimate even if its TPS input is unavailable.
		evaluation.Map = fallbackMap;
		evaluation.UsesEstimate = true;
	} else if (
			engineConfiguration->useMapEstimateDuringTransient &&
			engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold) {
		// Take the greater of real or estimated map so we don't under-fuel on a transient
		evaluation.Map = std::max(map.Value, fallbackMap);
		evaluation.UsesEstimate = map.Value < fallbackMap;
	} else {
		// Normal operation,
		evaluation.Map = map.Value;
	}
	evaluation.Valid = std::isfinite(evaluation.Map) && evaluation.Map >= 0 &&
					   (!evaluation.UsesEstimate || (tps.Valid && std::isfinite(tps.Value) && std::isfinite(rpm)));
	return evaluation;
}

void SpeedDensityAirmass::captureInputs(float rpm, AirmassInputs& inputs) const {
	inputs.Rpm = rpm;
	inputs.Tps = Sensor::get(SensorType::Tps1);
	inputs.MeasuredMap = Sensor::get(SensorType::Map);
	inputs.Iat = Sensor::get(SensorType::Iat);
	inputs.Pedal = Sensor::get(SensorType::AcceleratorPedal);
	inputs.ChargeTemperatureK = engine->engineState.sd.tChargeK;
	inputs.Displacement = engineConfiguration->displacement;
	inputs.CylinderCount = engine->engineState.cylinderCount;
	inputs.PreviousFuelingLoad = getFuelingLoad();
	inputs.PreviousIgnitionLoad = getIgnitionLoad();
	inputs.LambdaOverride = engineConfiguration->afrOverrideMode;
	inputs.IgnitionOverride = engineConfiguration->ignOverrideMode;
	inputs.AlphaNUseIat = engineConfiguration->alphaNUseIat;
	inputs.DedicatedTables = engineConfiguration->useDedicatedAirmassTables;
	inputs.ConfigurationValid = isRawAirmassConfigurationValid();

	// Read each core source exactly once. The estimate lookup and selection use
	// those captured samples; the explicit MAP override retains MeasuredMap.
	auto& map = inputs.EffectiveMap;
	map.Map = inputs.MeasuredMap.value_or(0);
	map.FallbackMap = 0;
	map.HasValue = false;
	map.UsesEstimate = !inputs.MeasuredMap;
	map.Valid = inputs.MeasuredMap.Valid && std::isfinite(rpm) && rpm >= 0 && std::isfinite(map.Map) && map.Map >= 0 &&
				map.Map <= 1000;
	const bool transient = engineConfiguration->useMapEstimateDuringTransient &&
						   engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold;
	if (inputs.MeasuredMap && !transient) {
		// An unused estimate neither invalidates measured MAP nor incurs a table
		// scan/interpolation. Unlike legacy diagnostics, no fallback is present.
		return;
	}
	map.Valid = false;
	if (!std::isfinite(rpm) || rpm < 0 || !inputs.Tps || !std::isfinite(inputs.Tps.Value) || inputs.Tps.Value < 0 ||
		inputs.Tps.Value > 100 || !isMapEstimateConfigurationValid()) {
		return;
	}
	map.FallbackMap = m_mapEstimationTable->getValue(rpm, inputs.Tps.Value);
	map.HasValue = true;
	map.UsesEstimate = !inputs.MeasuredMap || (transient && inputs.MeasuredMap.Value < map.FallbackMap);
	map.Map = map.UsesEstimate ? map.FallbackMap : inputs.MeasuredMap.Value;
	// Readiness is a separate activation declaration in the later composite mode.
	// Transient max selection needs a usable estimate even when measured MAP wins.
	map.Valid = std::isfinite(map.FallbackMap) && map.FallbackMap >= 0 && map.FallbackMap <= 600 &&
				std::isfinite(map.Map) && map.Map >= 0 && map.Map <= 1000;
}

AirmassEvaluation
SpeedDensityAirmass::evaluateRawAirmass(const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics) const {
	if (diagnostics) {
		diagnostics->HasValue = false;
		diagnostics->Valid = false;
	}
	AirmassEvaluation evaluation;
	if (!inputs.DedicatedTables || !inputs.ConfigurationValid || !std::isfinite(inputs.Rpm) || inputs.Rpm <= 0 ||
		!inputs.EffectiveMap.Valid || !std::isfinite(inputs.EffectiveMap.Map) || inputs.EffectiveMap.Map < 0 ||
		inputs.EffectiveMap.Map > 1000 || !std::isfinite(inputs.ChargeTemperatureK) || inputs.ChargeTemperatureK <= 0 ||
		!std::isfinite(inputs.Displacement) || inputs.Displacement <= 0 || !std::isfinite(inputs.CylinderCount) ||
		inputs.CylinderCount <= 0) {
		return evaluation;
	}
	auto ve = evaluateRawVe(inputs.Rpm, inputs.EffectiveMap.Map, diagnostics);
	const float mass = getAirmassImpl(
			ve.Ve * PERCENT_DIV,
			inputs.EffectiveMap.Map,
			inputs.ChargeTemperatureK,
			inputs.Displacement,
			inputs.CylinderCount);
	evaluation.Result = {mass, inputs.EffectiveMap.Map};
	evaluation.Valid = ve.Valid && std::isfinite(mass) && mass >= 0;
	return evaluation;
}

float SpeedDensityAirmass::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
