#include "pch.h"

#include "alphan_airmass.h"
#include "fuel_math.h"
#include "maf_airmass.h"

namespace {
constexpr float AirGasConstant = 0.28705f;

mass_t
expectedIdealGasMass(float displacement, int cylinders, float fillingPercent, float pressure, float temperatureK) {
	return (fillingPercent * 0.01f) * displacement * pressure / (AirGasConstant * temperatureK) / cylinders;
}

struct DedicatedTableIdleController : public MockIdleController {
	bool isIdlingOrTaper() const override {
		return true;
	}
};

void seedPublishedVeDiagnostics() {
	engine->engineState.currentVe = 91;
	engine->engineState.veTableYAxis = 92;
	engine->engineState.idleVeTableYAxis = 93;
}

void expectPublishedVeDiagnosticsUnchanged() {
	EXPECT_FLOAT_EQ(engine->engineState.currentVe, 91);
	EXPECT_FLOAT_EQ(engine->engineState.veTableYAxis, 92);
	EXPECT_FLOAT_EQ(engine->engineState.idleVeTableYAxis, 93);
}

void configureAlphaNAxesForInterpolation() {
	setLinearCurve(config->alphaNTpsBins, 0, 7.5f, 0.1f);
	setLinearCurve(config->alphaNRpmBins, 500, 8000, 1);
	setTable(config->alphaNTable, 10);

	// At TPS 1.25 and 1750 rpm all four interpolation weights are 0.5.
	config->alphaNTable[2][2] = 40;
	config->alphaNTable[3][2] = 60;
	config->alphaNTable[2][3] = 80;
	config->alphaNTable[3][3] = 100;
	config->alphaNTable[0][0] = 33;
	config->alphaNTable[ALPHA_N_LOAD_COUNT - 1][ALPHA_N_RPM_COUNT - 1] = 99;
}

void configureMafAxesForInterpolation() {
	setLinearCurve(config->mafLoadBins, 0, 150, 1);
	setLinearCurve(config->mafRpmBins, 500, 8000, 1);
	setTable(config->mafTable, 10);

	// At 55% filling and 1750 rpm all four interpolation weights are 0.5.
	config->mafTable[5][2] = 40;
	config->mafTable[6][2] = 60;
	config->mafTable[5][3] = 80;
	config->mafTable[6][3] = 100;
}
} // namespace

TEST(DedicatedAirmassTables, DefaultsKeepLegacyPresetBehavior) {
	EngineTestHelper eth(engine_type_e::FRANKENSO_BMW_M73_F);
	Sensor::setMockValue(SensorType::Tps1, 25);

	EXPECT_FALSE(engineConfiguration->useDedicatedAirmassTables);
	EXPECT_EQ(ALPHA_N_LOAD_COUNT, 16);
	EXPECT_EQ(ALPHA_N_RPM_COUNT, 16);
	EXPECT_EQ(MAF_LOAD_COUNT, 16);
	EXPECT_EQ(MAF_RPM_COUNT, 16);
	EXPECT_FLOAT_EQ(config->veTable[0][0], 45);
	EXPECT_FLOAT_EQ(config->alphaNTpsBins[1], 0.5f);
	EXPECT_FLOAT_EQ(config->mafLoadBins[MAF_LOAD_COUNT - 1], 200);
	for (size_t load = 0; load < ALPHA_N_LOAD_COUNT; load++) {
		for (size_t rpm = 0; rpm < ALPHA_N_RPM_COUNT; rpm++) {
			EXPECT_FLOAT_EQ(config->alphaNTable[load][rpm], 80);
		}
	}
	for (size_t load = 0; load < MAF_LOAD_COUNT; load++) {
		for (size_t rpm = 0; rpm < MAF_RPM_COUNT; rpm++) {
			EXPECT_FLOAT_EQ(config->mafTable[load][rpm], 100);
		}
	}

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	auto evaluation = alphaN.evaluateAirmass(1800, &diagnostics);

	EXPECT_TRUE(evaluation.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 45);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 25);
	EXPECT_NEAR(
			evaluation.Result.CylinderAirmass,
			expectedIdealGasMass(
					engineConfiguration->displacement, engine->engineState.cylinderCount, 45, 101.325f, 293),
			EPS4D);
}

TEST(DedicatedAirmassTables, AlphaNUsesFractionalTpsAndIndependentRpmAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->fuelAlgorithm = LM_REAL_MAF;
	engineConfiguration->displacement = 3.2f;
	setCylinderCount(4);
	configureAlphaNAxesForInterpolation();

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	Sensor::setMockValue(SensorType::Tps1, 1.25f);
	auto midpoint = alphaN.evaluateAirmass(1750, &diagnostics);
	EXPECT_TRUE(midpoint.Valid);
	EXPECT_FLOAT_EQ(midpoint.Result.EngineLoadPercent, 1.25f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 70);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 1.25f);
	EXPECT_NEAR(midpoint.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 70, 101.325f, 293), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 1.1f);
	auto asymmetric = alphaN.evaluateAirmass(1875, &diagnostics);
	EXPECT_TRUE(asymmetric.Valid);
	EXPECT_NEAR(asymmetric.Result.EngineLoadPercent, 1.1f, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Ve, 74, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Load, 1.1f, EPS4D);
	EXPECT_NEAR(asymmetric.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 74, 101.325f, 293), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 0);
	auto belowAxes = alphaN.evaluateAirmass(100, &diagnostics);
	EXPECT_TRUE(belowAxes.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 33);
	EXPECT_NEAR(belowAxes.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 33, 101.325f, 293), EPS4D);

	Sensor::setMockValue(SensorType::Tps1, 100);
	auto aboveAxes = alphaN.evaluateAirmass(9000, &diagnostics);
	EXPECT_TRUE(aboveAxes.Valid);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 99);
	EXPECT_NEAR(aboveAxes.Result.CylinderAirmass, expectedIdealGasMass(3.2f, 4, 99, 101.325f, 293), EPS4D);
}

TEST(DedicatedAirmassTables, MafUsesNativeFillingAndIndependentRpmAxes) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	engineConfiguration->displacement = 2.0f;
	setCylinderCount(4);
	configureMafAxesForInterpolation();

	constexpr float rpm = 1750;
	constexpr float nativeLoad = 55;
	const float rawCylinderMass = nativeLoad * 0.01f * getStandardAirCharge();
	const float mafKgPerHour = rawCylinderMass * rpm * engine->engineState.cylinderCount * 0.03f;

	MafAirmass maf;
	AirmassDiagnostics diagnostics;
	auto evaluation = maf.evaluateAirmassImpl(mafKgPerHour, rpm, &diagnostics);

	EXPECT_TRUE(evaluation.Valid);
	EXPECT_NEAR(evaluation.Result.EngineLoadPercent, nativeLoad, EPS4D);
	EXPECT_NEAR(diagnostics.Ve.Load, nativeLoad, EPS4D);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 70);
	EXPECT_NEAR(evaluation.Result.CylinderAirmass, rawCylinderMass * 0.70f, EPS4D);
}

TEST(DedicatedAirmassTables, DisabledModePreservesLegacyLoadOverride) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = false;
	engineConfiguration->veOverrideMode = VE_MAP;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	setTable(config->veTable, 60);
	setTable(config->alphaNTable, 25);
	Sensor::setMockValue(SensorType::Tps1, 12);
	Sensor::setMockValue(SensorType::Map, 75);

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	auto evaluation = alphaN.evaluateAirmass(2200, &diagnostics);

	EXPECT_TRUE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Result.EngineLoadPercent, 12);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 75);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 60);
	EXPECT_NEAR(evaluation.Result.CylinderAirmass, expectedIdealGasMass(2.4f, 4, 60, 101.325f, 293), EPS4D);
	EXPECT_TRUE(isAirmassConfigurationValid());

	engineConfiguration->useDedicatedAirmassTables = true;
	EXPECT_FALSE(isAirmassConfigurationValid());
	seedPublishedVeDiagnostics();
	const int warningCount = eth.getWarningCounter();

	AirmassEvaluation invalidEvaluation;
	EXPECT_NO_FATAL_ERROR(invalidEvaluation = alphaN.evaluateAirmass(2200, &diagnostics));
	EXPECT_FALSE(invalidEvaluation.Valid);
	EXPECT_FLOAT_EQ(invalidEvaluation.Result.CylinderAirmass, 0);
	EXPECT_FLOAT_EQ(invalidEvaluation.Result.EngineLoadPercent, 12);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);

	AirmassResult dryResult;
	EXPECT_NO_FATAL_ERROR(dryResult = alphaN.getAirmass(2200, false));
	EXPECT_FLOAT_EQ(dryResult.CylinderAirmass, 0);
	EXPECT_FLOAT_EQ(dryResult.EngineLoadPercent, 12);
	EXPECT_EQ(eth.getWarningCounter(), warningCount);
	EXPECT_FALSE(hasFirmwareError());
	expectPublishedVeDiagnosticsUnchanged();
}

TEST(DedicatedAirmassTables, ConfigurationChangeRejectsInvalidDedicatedConfiguration) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->veOverrideMode = VE_TPS;
	const auto version = engine->globalConfigurationVersion;

	EXPECT_FATAL_ERROR(incrementGlobalConfigurationVersion());
	EXPECT_EQ(engine->globalConfigurationVersion, version);

	engineConfiguration->veOverrideMode = VE_None;
	config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1] = 18001;
	EXPECT_FATAL_ERROR(incrementGlobalConfigurationVersion());
	EXPECT_EQ(engine->globalConfigurationVersion, version);
}

TEST(DedicatedAirmassTables, DedicatedAxesMustBeStrictlyAscending) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	ASSERT_TRUE(isAirmassConfigurationValid());

	const float alphaTps = config->alphaNTpsBins[2];
	config->alphaNTpsBins[2] = config->alphaNTpsBins[1];
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->alphaNTpsBins[2] = alphaTps;

	const float alphaRpm = config->alphaNRpmBins[3];
	config->alphaNRpmBins[3] = config->alphaNRpmBins[1];
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->alphaNRpmBins[3] = alphaRpm;

	const float mafLoad = config->mafLoadBins[2];
	config->mafLoadBins[2] = config->mafLoadBins[1];
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->mafLoadBins[2] = mafLoad;

	const float mafRpm = config->mafRpmBins[3];
	config->mafRpmBins[3] = config->mafRpmBins[1];
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->mafRpmBins[3] = mafRpm;

	const float alphaTpsLast = config->alphaNTpsBins[ALPHA_N_LOAD_COUNT - 1];
	config->alphaNTpsBins[ALPHA_N_LOAD_COUNT - 1] = 100.01f;
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->alphaNTpsBins[ALPHA_N_LOAD_COUNT - 1] = alphaTpsLast;

	const float alphaRpmLast = config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1];
	config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1] = 18001;
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->alphaNRpmBins[ALPHA_N_RPM_COUNT - 1] = alphaRpmLast;

	const float mafLoadLast = config->mafLoadBins[MAF_LOAD_COUNT - 1];
	config->mafLoadBins[MAF_LOAD_COUNT - 1] = 1001;
	EXPECT_FALSE(isAirmassConfigurationValid());
	engineConfiguration->useDedicatedAirmassTables = false;
	EXPECT_TRUE(isAirmassConfigurationValid());
	engineConfiguration->useDedicatedAirmassTables = true;
	config->mafLoadBins[MAF_LOAD_COUNT - 1] = mafLoadLast;

	const float mafRpmLast = config->mafRpmBins[MAF_RPM_COUNT - 1];
	config->mafRpmBins[MAF_RPM_COUNT - 1] = 18001;
	EXPECT_FALSE(isAirmassConfigurationValid());
	config->mafRpmBins[MAF_RPM_COUNT - 1] = mafRpmLast;
	EXPECT_TRUE(isAirmassConfigurationValid());
}

TEST(DedicatedAirmassTables, InvalidAxisDryReadDoesNotPublishOrRaiseFault) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	config->alphaNTpsBins[2] = config->alphaNTpsBins[1];
	Sensor::setMockValue(SensorType::Tps1, 12);
	seedPublishedVeDiagnostics();
	const int warningCount = eth.getWarningCounter();

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	AirmassEvaluation evaluation;
	EXPECT_NO_FATAL_ERROR(evaluation = alphaN.evaluateAirmass(2200, &diagnostics));
	EXPECT_FALSE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Result.CylinderAirmass, 0);
	EXPECT_FLOAT_EQ(evaluation.Result.EngineLoadPercent, 12);
	EXPECT_FALSE(diagnostics.Ve.HasValue);
	EXPECT_FALSE(diagnostics.Ve.Valid);
	EXPECT_EQ(eth.getWarningCounter(), warningCount);
	EXPECT_FALSE(hasFirmwareError());
	expectPublishedVeDiagnosticsUnchanged();
}

TEST(DedicatedAirmassTables, LiveFuelOwnerRejectsInvalidUnselectedAxis) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->fuelAlgorithm = LM_ALPHA_N;
	config->mafLoadBins[MAF_LOAD_COUNT - 1] = 1001;

	EXPECT_FATAL_ERROR(getCycleInjectionMass(2200, false));
}

TEST(DedicatedAirmassTables, DedicatedMainAxisRetainsIdleOverride) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engineConfiguration->useDedicatedAirmassTables = true;
	engineConfiguration->veOverrideMode = VE_None;
	engineConfiguration->useSeparateVeForIdle = true;
	engineConfiguration->idleVeOverrideMode = VE_MAP;
	engineConfiguration->idlePidDeactivationTpsThreshold = 10;
	engineConfiguration->displacement = 2.4f;
	setCylinderCount(4);
	setTable(config->alphaNTable, 80);
	setTable(config->idleVeTable, 40);
	setLinearCurve(config->idleVeLoadBins, 0, 100, 1);
	setLinearCurve(config->idleVeRpmBins, 0, 7000, 1);

	DedicatedTableIdleController idle;
	engine->engineModules.get<IdleController>().set(&idle);
	Sensor::setMockValue(SensorType::Tps1, 12.5f);
	Sensor::setMockValue(SensorType::Map, 77);
	Sensor::setMockValue(SensorType::DriverThrottleIntent, 7.5f);

	AlphaNAirmass alphaN;
	AirmassDiagnostics diagnostics;
	auto evaluation = alphaN.evaluateAirmass(2300, &diagnostics);

	// The dedicated TPS-axis value is 80. Idle MAP override produces 40, and taper is halfway between them.
	EXPECT_TRUE(evaluation.Valid);
	EXPECT_FLOAT_EQ(evaluation.Result.EngineLoadPercent, 12.5f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Load, 12.5f);
	EXPECT_FLOAT_EQ(diagnostics.Ve.IdleLoad, 77);
	EXPECT_FLOAT_EQ(diagnostics.Ve.Ve, 60);
	EXPECT_NEAR(evaluation.Result.CylinderAirmass, expectedIdealGasMass(2.4f, 4, 60, 101.325f, 293), EPS4D);
}
