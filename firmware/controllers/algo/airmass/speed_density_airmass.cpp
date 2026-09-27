#include "pch.h"
#include "speed_density_airmass.h"

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, bool postState) {
	ScopePerf perf(PE::GetSpeedDensityFuel);

	auto evaluation = evaluateAirmass(rpm);
	if (postState) {
		publishEvaluation(evaluation);
	}
	return evaluation.Result;
}

AirmassResult SpeedDensityAirmass::getAirmass(float rpm, float map, bool postState) {
	auto evaluation = evaluateAirmass(rpm, map);
	if (postState) {
		publishEvaluation(evaluation);
	}
	return evaluation.Result;
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm) const {
	auto map = evaluateMap(rpm);
	auto evaluation = evaluateAirmass(rpm, map.Map);
	evaluation.Map = map;
	evaluation.Valid = evaluation.Valid && map.Valid;
	return evaluation;
}

AirmassEvaluation SpeedDensityAirmass::evaluateAirmass(float rpm, float map) const {
	AirmassEvaluation evaluation;
	evaluation.Map.Map = map;
	evaluation.Map.Valid = std::isfinite(map) && map >= 0;
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

	evaluateVe(rpm, map, evaluation.Ve);
	float ve = evaluation.Ve.Ve * PERCENT_DIV;

	float airMass = getAirmassImpl(ve, map, tChargeK);
	if (std::isnan(airMass)) {
		warning(ObdCode::CUSTOM_ERR_6685, "NaN airMass");
		return evaluation;
	}

	evaluation.Result = {
			airMass,
			map, // AFR/VE table Y axis
	};
	evaluation.Valid = evaluation.Map.Valid && evaluation.Ve.Valid && std::isfinite(tChargeK) && tChargeK > 0 &&
					   std::isfinite(airMass) && airMass >= 0;
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

#if EFI_TUNER_STUDIO
	if (postState) {
		engine->outputChannels.fallbackMap = evaluation.FallbackMap;
	}
#endif // EFI_TUNER_STUDIO
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

float SpeedDensityAirmass::getVeImpl(float rpm, percent_t load) const {
	return interpolate3d(config->veTable, config->veLoadBins, load, config->veRpmBins, rpm);
}
