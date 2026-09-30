#include "pch.h"
#include "airmass_loads.h"

struct ConsumerLambdaMonitor : public LambdaMonitor {
	using LambdaMonitorBase::isCurrentlyGood;
	using LambdaMonitorBase::restoreConditionsMet;
};

TEST(LambdaMonitor, DeviationTableAndThresholdsHaveIndependentCoordinates) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	setTimeNowUs(10e6);
	Sensor::setMockValue(SensorType::Map, 80);
	Sensor::setMockValue(SensorType::Tps1, 20);
	Sensor::setMockValue(SensorType::Lambda1, 1.10f);
	engineConfiguration->lambdaProtectionEnable = true;
	engineConfiguration->lambdaProtectionMinRpm = 1000;
	engineConfiguration->lambdaProtectionMinLoad = 50;
	engineConfiguration->lambdaProtectionMinTps = 0;
	engineConfiguration->lambdaProtectionRestoreRpm = 3000;
	engineConfiguration->lambdaProtectionRestoreLoad = 50;
	engineConfiguration->lambdaProtectionRestoreTps = 100;
	engine->fuelComputer.targetLambda = 1;
	config->lambdaMonitorLoadSource = AFR_MAP;
	config->lambdaDeviationLoadSource = AFR_Tps;
	setLinearCurve(config->lambdaMaxDeviationLoadBins, 0, 100, 1);
	for (size_t row = 0; row < efi::size(config->lambdaMaxDeviationTable); row++) {
		setArrayValues(config->lambdaMaxDeviationTable[row], 0.002f * config->lambdaMaxDeviationLoadBins[row]);
	}
	ConsumerLambdaMonitor monitor;
	const auto controlLoad = [] { return getAirmassConsumerLoad(AirmassConsumer::LambdaMonitor); };
	EXPECT_FALSE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, controlLoad()));

	config->lambdaDeviationLoadSource = AFR_MAP;
	EXPECT_TRUE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, controlLoad()));

	config->lambdaDeviationLoadSource = AFR_Tps;
	config->lambdaMonitorLoadSource = AFR_Tps;
	EXPECT_TRUE(monitor.isCurrentlyGood(2000, controlLoad()));
	EXPECT_TRUE(monitor.restoreConditionsMet(2000, controlLoad()));
	EXPECT_FALSE(monitor.isCurrentlyGood(2000, NAN));
	EXPECT_FALSE(monitor.restoreConditionsMet(2000, NAN));
}

struct MockLambdaMonitor : public LambdaMonitorBase {
	bool isGood = true;
	bool isCurrentlyGood(float /*rpm*/, float /*load*/) const override {
		return isGood;
	}

	bool isRestore = false;
	bool restoreConditionsMet(float /*rpm*/, float /*load*/) const override {
		return isRestore;
	}

	float getTimeout() const override {
		// Timeout of 1 second
		return 1;
	}

	MOCK_METHOD(float, getMaxAllowedLambda, (float rpm, float load), (const, override));
};

TEST(LambdaMonitor, Response) {
	MockLambdaMonitor mlm;

	int startTime = 1e6;
	setTimeNowUs(startTime);

	mlm.isGood = true;
	mlm.isRestore = false;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// now lambda will be bad, but we don't cut yet
	mlm.isGood = false;
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// 0.9 second later, still not cut
	setTimeNowUs(startTime + 0.9e6);
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());

	// 1.1 second later, cut!
	setTimeNowUs(startTime + 1.1e6);
	mlm.update(2000, 50);
	EXPECT_FALSE(mlm.lambdaCurrentlyGood);
	EXPECT_TRUE(mlm.isCut());

	// Lambda goes back to normal, but restore conditions not met so we should stay cut
	mlm.isGood = true;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_TRUE(mlm.isCut());

	mlm.isRestore = true;
	mlm.update(2000, 50);
	EXPECT_TRUE(mlm.lambdaCurrentlyGood);
	EXPECT_FALSE(mlm.isCut());
}
