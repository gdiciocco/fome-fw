#include "pch.h"

#include "alphan_airmass.h"
#include "fuel_math.h"
#include "speed_density_airmass.h"

using ::testing::Return;
using ::testing::StrictMock;

namespace {
constexpr float AirGasConstant = 0.28705f;

mass_t expectedIdealGasMass(float displacement, int cylinders, float vePercent, float pressure, float temperatureK) {
	return (vePercent * 0.01f) * displacement * pressure / (AirGasConstant * temperatureK) / cylinders;
}

void configureFlatVeBlend(blend_table_s& blend, gppwm_channel_e parameter, gppwm_channel_e yAxis, float correction) {
	blend.blendParameter = parameter;
	blend.yAxisOverride = yAxis;
	setTable(blend.table, correction);
	setLinearCurve(blend.loadBins, 0, 100, 1);
	setLinearCurve(blend.rpmBins, 0, 7000, 1);
	setLinearCurve(blend.blendBins, 0, 100, 1);
	setArrayValues(blend.blendValues, 100);
}

void expectLoad(
		const AirmassLoad& load,
		float value,
		AirmassLoadSource source,
		AirmassLoadUnit unit,
		bool usesEstimate = false) {
	EXPECT_TRUE(load.Valid);
	EXPECT_FLOAT_EQ(load.Value, value);
	EXPECT_EQ(load.Source, source);
	EXPECT_EQ(load.Unit, unit);
	EXPECT_EQ(load.UsesEstimate, usesEstimate);
}

class CountingSensor final : public Sensor {
public:
	CountingSensor(SensorType type, float value)
		: Sensor(type)
		, m_value(value) {}

	SensorResult get() const override {
		ReadCount++;
		return m_value;
	}

	void showInfo(const char*) const override {}

	mutable size_t ReadCount = 0;

private:
	float m_value;
};
} // namespace

TEST(AirmassContext, CapturePreservesMapProvenanceAndResolvesEveryLoadSource) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	engine->engineState.fuelingLoad = 61;
	engine->engineState.ignitionLoad = 62;
	engineConfiguration->afrOverrideMode = AFR_MAP;
	engineConfiguration->ignOverrideMode = AFR_Tps;
	engineConfiguration->useMapEstimateDuringTransient = true;
	engine->module<TpsAccelEnrichment>()->isAboveAccelThreshold = true;
	Sensor::setMockValue(SensorType::Map, 40);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 30);
	Sensor::setMockValue(SensorType::Iat, 35);

	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(3000, 20)).Times(2).WillRepeatedly(Return(75));
	SpeedDensityAirmass sd(nullptr, mapEstimate);

	AirmassInputs inputs;
	sd.captureInputs(3000, inputs);

	ASSERT_TRUE(inputs.MeasuredMap.Valid);
	EXPECT_FLOAT_EQ(inputs.MeasuredMap.Value, 40);
	EXPECT_TRUE(inputs.EffectiveMap.HasValue);
	EXPECT_TRUE(inputs.EffectiveMap.Valid);
	EXPECT_TRUE(inputs.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(inputs.EffectiveMap.Map, 75);
	EXPECT_FLOAT_EQ(inputs.EffectiveMap.FallbackMap, 75);
	EXPECT_FLOAT_EQ(inputs.PreviousFuelingLoad, 61);
	EXPECT_FLOAT_EQ(inputs.PreviousIgnitionLoad, 62);
	EXPECT_EQ(inputs.LambdaOverride, AFR_MAP);
	EXPECT_EQ(inputs.IgnitionOverride, AFR_Tps);

	// Downstream resolution must use this capture, even after live inputs change.
	Sensor::setMockValue(SensorType::Map, 90);
	Sensor::setMockValue(SensorType::Tps1, 91);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 92);
	engine->engineState.fuelingLoad = 93;
	engine->engineState.ignitionLoad = 94;

	expectLoad(
			resolveAirmassLoad(inputs, 0, AFR_None), 75, AirmassLoadSource::EffectiveMap, AirmassLoadUnit::Kpa, true);
	expectLoad(
			resolveAirmassLoad(inputs, 0, inputs.LambdaOverride),
			40,
			AirmassLoadSource::MeasuredMap,
			AirmassLoadUnit::Kpa);
	expectLoad(
			resolveAirmassLoad(inputs, 0, inputs.IgnitionOverride),
			20,
			AirmassLoadSource::Tps,
			AirmassLoadUnit::Percent);
	expectLoad(resolveAirmassLoad(inputs, 0, AFR_AccPedal), 30, AirmassLoadSource::Pedal, AirmassLoadUnit::Percent);

	const float halfStandardCharge = 0.5f * getStandardAirCharge();
	expectLoad(
			resolveAirmassLoad(inputs, halfStandardCharge, AFR_CylFilling),
			50,
			AirmassLoadSource::CylinderFilling,
			AirmassLoadUnit::Percent);

	// A usable estimate does not make the physically measured MAP source valid.
	inputs.MeasuredMap = unexpected;
	EXPECT_TRUE(resolveAirmassLoad(inputs, 0, AFR_None).Valid);
	auto missingMeasuredMap = resolveAirmassLoad(inputs, 0, AFR_MAP);
	EXPECT_FALSE(missingMeasuredMap.Valid);
	EXPECT_EQ(missingMeasuredMap.Source, AirmassLoadSource::MeasuredMap);

	Sensor::setInvalidMockValue(SensorType::Map);
	Sensor::setMockValue(SensorType::Tps1, 20);
	AirmassInputs estimatedOnly;
	sd.captureInputs(3000, estimatedOnly);
	EXPECT_FALSE(estimatedOnly.MeasuredMap.Valid);
	EXPECT_TRUE(estimatedOnly.EffectiveMap.Valid);
	EXPECT_TRUE(estimatedOnly.EffectiveMap.UsesEstimate);

	Sensor::setInvalidMockValue(SensorType::Tps1);
	AirmassInputs invalidEstimate;
	sd.captureInputs(3000, invalidEstimate);
	EXPECT_TRUE(invalidEstimate.EffectiveMap.UsesEstimate);
	EXPECT_FALSE(invalidEstimate.EffectiveMap.Valid);
	EXPECT_FALSE(resolveAirmassLoad(invalidEstimate, 0, AFR_None).Valid);
}

TEST(AirmassContext, StrictCaptureValidatesAnEstimateBeforeInterpolation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	Sensor::setMockValue(SensorType::Map, 55);
	Sensor::setMockValue(SensorType::Tps1, 20);

	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	const float originalTpsBin = config->mapEstimateTpsBins[2];
	const float originalCell = config->mapEstimateTable[0][0];
	config->mapEstimateTpsBins[2] = config->mapEstimateTpsBins[1];
	config->mapEstimateTable[0][0] = 650;

	// A healthy measured MAP does not depend on an unused estimate calibration.
	AirmassInputs measuredOnly;
	sd.captureInputs(2600, measuredOnly);
	EXPECT_TRUE(measuredOnly.EffectiveMap.Valid);
	EXPECT_FALSE(measuredOnly.EffectiveMap.HasValue);
	EXPECT_FALSE(measuredOnly.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(measuredOnly.EffectiveMap.Map, 55);

	Sensor::setInvalidMockValue(SensorType::Map);
	AirmassInputs invalidAxes;
	sd.captureInputs(2600, invalidAxes);
	EXPECT_FALSE(invalidAxes.EffectiveMap.Valid);

	config->mapEstimateTpsBins[2] = originalTpsBin;
	AirmassInputs invalidCell;
	sd.captureInputs(2600, invalidCell);
	EXPECT_FALSE(invalidCell.EffectiveMap.Valid);

	config->mapEstimateTable[0][0] = originalCell;
	Sensor::setInvalidMockValue(SensorType::Tps1);
	AirmassInputs invalidTps;
	sd.captureInputs(2600, invalidTps);
	EXPECT_FALSE(invalidTps.EffectiveMap.Valid);

	Sensor::setMockValue(SensorType::Tps1, 20);
	EXPECT_CALL(mapEstimate, getValue(2600, 20)).WillOnce(Return(70));
	AirmassInputs validEstimate;
	sd.captureInputs(2600, validEstimate);
	EXPECT_TRUE(validEstimate.EffectiveMap.Valid);
	EXPECT_TRUE(validEstimate.EffectiveMap.HasValue);
	EXPECT_TRUE(validEstimate.EffectiveMap.UsesEstimate);
	EXPECT_FLOAT_EQ(validEstimate.EffectiveMap.Map, 70);
}

TEST(AirmassContext, RawModelsAndCorrectionsUseOneSnapshotAndMatchStandaloneResults) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->displacement = 3.2f;
	engineConfiguration->alphaNUseIat = true;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 50);
	configureFlatVeBlend(config->veBlends[0], GPPWM_Tps, GPPWM_Map, 20);
	configureFlatVeBlend(config->veBlends[1], GPPWM_Iat, GPPWM_FuelLoad, -25);
	engine->engineState.fuelingLoad = 64;
	Sensor::setMockValue(SensorType::Map, 45);
	Sensor::setMockValue(SensorType::Tps1, 22);
	Sensor::setMockValue(SensorType::AcceleratorPedal, 31);
	Sensor::setMockValue(SensorType::Iat, 27);

	StrictMock<MockVp3d> mapEstimate;
	EXPECT_CALL(mapEstimate, getValue(2400, 22)).WillOnce(Return(70));
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;

	AirmassInputs inputs;
	sd.captureInputs(2400, inputs);
	const auto standaloneSd = sd.evaluateAirmass(2400);
	const auto standaloneAlphaN = alphaN.evaluateAirmass(2400);

	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 81);
	Sensor::setMockValue(SensorType::Iat, 82);
	engine->engineState.fuelingLoad = 83;

	RawAirmassDiagnostics sdDiagnostics;
	RawAirmassDiagnostics alphaNDiagnostics;
	const auto rawSd = sd.evaluateRawAirmass(inputs, &sdDiagnostics);
	const auto rawAlphaN = alphaN.evaluateRawAirmass(inputs, &alphaNDiagnostics);
	VeCorrectionDiagnostics correctionDiagnostics;
	const auto corrections = evaluateAirmassCorrections(inputs, &correctionDiagnostics);

	ASSERT_TRUE(rawSd.Valid);
	ASSERT_TRUE(rawAlphaN.Valid);
	ASSERT_TRUE(corrections.Valid);
	EXPECT_TRUE(sdDiagnostics.HasValue);
	EXPECT_TRUE(sdDiagnostics.Valid);
	EXPECT_FLOAT_EQ(sdDiagnostics.TableValue, 60);
	EXPECT_TRUE(alphaNDiagnostics.HasValue);
	EXPECT_TRUE(alphaNDiagnostics.Valid);
	EXPECT_FLOAT_EQ(alphaNDiagnostics.TableValue, 50);
	EXPECT_NEAR(rawSd.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 60, 45, 310), EPS4D);
	EXPECT_FLOAT_EQ(rawSd.Result.EngineLoadPercent, 45);
	EXPECT_NEAR(rawAlphaN.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 50, 101.325f, 300), EPS4D);
	EXPECT_FLOAT_EQ(rawAlphaN.Result.EngineLoadPercent, 22);

	// Raw model values exclude common corrections. +20% and -25% compound to 0.9.
	EXPECT_FLOAT_EQ(corrections.Multiplier, 0.9f);
	EXPECT_TRUE(correctionDiagnostics.HasValue);
	EXPECT_TRUE(correctionDiagnostics.Valid);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[0].BlendParameter, 22);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[0].TableYAxis, 45);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[1].BlendParameter, 27);
	EXPECT_FLOAT_EQ(correctionDiagnostics.Blends[1].TableYAxis, 64);
	EXPECT_NEAR(standaloneSd.Result.CylinderAirmass, rawSd.Result.CylinderAirmass * corrections.Multiplier, EPS4D);
	EXPECT_NEAR(
			standaloneAlphaN.Result.CylinderAirmass, rawAlphaN.Result.CylinderAirmass * corrections.Multiplier, EPS4D);
	EXPECT_FLOAT_EQ(standaloneSd.Result.EngineLoadPercent, rawSd.Result.EngineLoadPercent);
	EXPECT_FLOAT_EQ(standaloneAlphaN.Result.EngineLoadPercent, rawAlphaN.Result.EngineLoadPercent);

	const float originalBin = config->veBlends[0].loadBins[2];
	config->veBlends[0].loadBins[2] = config->veBlends[0].loadBins[1];
	const auto invalidAxis = evaluateAirmassCorrections(inputs);
	EXPECT_FALSE(invalidAxis.Valid);
	config->veBlends[0].loadBins[2] = originalBin;
}

TEST(AirmassContext, MissingIatIsStrictForRawAlphaNButPreservesStandaloneFallback) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->displacement = 4.0f;
	engineConfiguration->alphaNUseIat = false;
	setCylinderCount(4);
	engine->engineState.sd.tChargeK = 310;
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 40);
	Sensor::setMockValue(SensorType::Map, 50);
	Sensor::setMockValue(SensorType::Tps1, 18);
	Sensor::setInvalidMockValue(SensorType::Iat);

	StrictMock<MockVp3d> mapEstimate;
	SpeedDensityAirmass sd(nullptr, mapEstimate);
	AlphaNAirmass alphaN;

	AirmassInputs inputs;
	sd.captureInputs(2000, inputs);
	ASSERT_FALSE(inputs.Iat.Valid);
	ASSERT_FALSE(inputs.AlphaNUseIat);

	const auto rawSd = sd.evaluateRawAirmass(inputs);
	const auto fixedTemperatureAlphaN = alphaN.evaluateRawAirmass(inputs);
	EXPECT_TRUE(rawSd.Valid);
	EXPECT_TRUE(fixedTemperatureAlphaN.Valid);
	EXPECT_NEAR(fixedTemperatureAlphaN.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 40, 101.325f, 293), EPS4D);

	auto requestedIat = inputs;
	requestedIat.AlphaNUseIat = true;
	RawAirmassDiagnostics diagnostics;
	const auto strictAlphaN = alphaN.evaluateRawAirmass(requestedIat, &diagnostics);
	EXPECT_FALSE(strictAlphaN.Valid);
	EXPECT_FLOAT_EQ(strictAlphaN.Result.CylinderAirmass, 0);
	EXPECT_FALSE(diagnostics.HasValue);
	EXPECT_FALSE(diagnostics.Valid);
	EXPECT_TRUE(sd.evaluateRawAirmass(requestedIat).Valid);

	// The legacy standalone path intentionally retains its historical 20 C fallback.
	engineConfiguration->alphaNUseIat = true;
	const auto standalone = alphaN.evaluateAirmass(2000);
	EXPECT_TRUE(standalone.Valid);
	EXPECT_NEAR(standalone.Result.CylinderAirmass, expectedIdealGasMass(4, 4, 40, 101.325f, 293), EPS4D);

	configureFlatVeBlend(config->veBlends[0], GPPWM_Iat, GPPWM_Zero, 10);
	VeCorrectionDiagnostics correctionDiagnostics;
	const auto correction = evaluateAirmassCorrections(requestedIat, &correctionDiagnostics);
	EXPECT_FALSE(correction.Valid);
	EXPECT_FLOAT_EQ(correction.Multiplier, 1);
	EXPECT_TRUE(correctionDiagnostics.HasValue);
	EXPECT_FALSE(correctionDiagnostics.Valid);
}

TEST(AirmassContext, RepeatedNonCoreCorrectionChannelIsSampledOnce) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	CountingSensor auxTemperature(SensorType::AuxTemp1, 33);
	ASSERT_TRUE(auxTemperature.Register());

	configureFlatVeBlend(config->veBlends[0], GPPWM_AuxTemp1, GPPWM_AuxTemp1, 10);
	configureFlatVeBlend(config->veBlends[1], GPPWM_AuxTemp1, GPPWM_AuxTemp1, 20);
	AirmassInputs inputs;
	inputs.Rpm = 2500;
	inputs.EffectiveMap = {60, 60, true, true, false};

	const auto correction = evaluateAirmassCorrections(inputs);
	EXPECT_TRUE(correction.Valid);
	EXPECT_FLOAT_EQ(correction.Multiplier, 1.32f);
	EXPECT_EQ(auxTemperature.ReadCount, 1u);

	auxTemperature.unregister();
}
