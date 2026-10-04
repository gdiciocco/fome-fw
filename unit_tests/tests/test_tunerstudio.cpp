#include "pch.h"
#include "tunerstudio.h"
#include "tunerstudio_io.h"
#include "status_loop.h"
#include "limp_manager.h"

static uint8_t st5TestBuffer[16000];

TEST(VeAnalyzeTelemetry, crankingAndWallModelDisable) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	WallFuelController wallController;
	engine->engineModules.get<WallFuelController>().set(&wallController);
	engine->rpmCalculator.setRpmValue(300);
	Sensor::setMockValue(SensorType::Rpm, 300);
	updateTunerStudioState();
	EXPECT_EQ(engine->outputChannels.veAnalyzeIsCranking, 1);

	engine->rpmCalculator.setRpmValue(1500);
	Sensor::setMockValue(SensorType::Rpm, 1500);
	engineConfiguration->wwaeTau = 0.3f;
	engineConfiguration->wwaeBeta = 0.3f;
	wallController.onFastCallback();
	engine->injectionEvents.elements[0].getWallFuel().wallFuelCorrection = -0.001f;
	updateTunerStudioState();
	EXPECT_EQ(engine->outputChannels.veAnalyzeIsCranking, 0);
	EXPECT_NEAR(engine->outputChannels.wallFuelCorrectionValue, -1.0f, 0.01f);

	// A previous correction must not keep filtering samples after the model is disabled.
	engineConfiguration->wwaeTau = 0;
	wallController.onFastCallback();
	updateTunerStudioState();
	EXPECT_FLOAT_EQ(engine->outputChannels.wallFuelCorrectionValue, 0);
}

TEST(VeAnalyzeTelemetry, cutRecoveryAndSaturation) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	engine->rpmCalculator.setRpmValue(1500);
	Sensor::setMockValue(SensorType::Rpm, 1500);
	auto limp = engine->module<LimpManager>();
	advanceTimeUs(10e6);
	// A transient Lua spark cut exercises the same ECU recovery timer as limiter cuts.
	engine->engineState.lua.luaIgnCut = true;
	limp->updateState(3000, getTimeNowNt());
	updateTunerStudioState();
	EXPECT_FLOAT_EQ(engine->outputChannels.veAnalyzeTimeSinceCut, 0);

	advanceTimeUs(1.5e6);
	engine->engineState.lua.luaIgnCut = false;
	limp->updateState(1500, getTimeNowNt());
	updateTunerStudioState();
	EXPECT_NEAR(engine->outputChannels.veAnalyzeTimeSinceCut, 1.5f, 0.01f);
	advanceTimeUs(1e6);
	updateTunerStudioState();
	EXPECT_NEAR(engine->outputChannels.veAnalyzeTimeSinceCut, 2.5f, 0.01f);

	advanceTimeUs(700e6);
	updateTunerStudioState();
	// Timer::getElapsedSeconds itself saturates at the platform's 32-bit tick horizon.
	EXPECT_GE(engine->outputChannels.veAnalyzeTimeSinceCut, 2.0f);
	EXPECT_LE(engine->outputChannels.veAnalyzeTimeSinceCut, 655.35f);
}

class BufferTsChannel : public TsChannelBase {
public:
	BufferTsChannel()
		: TsChannelBase("Test") {}

	void write(const uint8_t* buffer, size_t size, bool /*isLastWriteInTransaction*/) override {
		memcpy(&st5TestBuffer[writeIdx], buffer, size);
		writeIdx += size;
	}

	size_t readTimeout(uint8_t* buffer, size_t size, int timeout) override {
		// nothing to do here
		return size;
	}

	void reset() {
		writeIdx = 0;
	}

	size_t writeIdx = 0;
};

#define PAYLOAD "123"
#define SIZE strlen(PAYLOAD)

static void assertCrcPacket(BufferTsChannel& dut) {
	ASSERT_EQ(dut.writeIdx, SIZE + 7);

	// todo: proper uint16 comparison
	ASSERT_EQ(st5TestBuffer[0], 0);
	ASSERT_EQ(st5TestBuffer[1], SIZE + 1);

	ASSERT_EQ(st5TestBuffer[2], TS_RESPONSE_OK);

	ASSERT_EQ(memcmp(&st5TestBuffer[3], PAYLOAD, SIZE), 0);

	// todo: proper uint32 comparison
	ASSERT_EQ(st5TestBuffer[6], 86);
	EXPECT_EQ(st5TestBuffer[7], 77);
	EXPECT_EQ(st5TestBuffer[8], 101);
	EXPECT_EQ(st5TestBuffer[9], 220);
}

TEST(binary, testWriteCrc) {
	BufferTsChannel test;

	// Small impl
	test.reset();
	test.copyAndWriteSmallCrcPacket((const uint8_t*)PAYLOAD, SIZE);
	assertCrcPacket(test);

	// Large impl
	test.reset();
	test.writeCrcPacketLocked((const uint8_t*)PAYLOAD, SIZE);
	assertCrcPacket(test);
}

TEST(TunerstudioCommands, writeChunkEngineConfig) {
	EngineTestHelper eth(engine_type_e::TEST_ENGINE);
	::testing::NiceMock<MockTsChannel> channel;

	uint8_t* configBytes = reinterpret_cast<uint8_t*>(config);

	// Contains zero before the write
	configBytes[100] = 0;
	EXPECT_EQ(configBytes[100], 0);

	// two step - writes to the engineConfiguration section require a burn
	uint8_t val = 50;
	TunerStudio instance;
	instance.handleWriteChunkCommand(&channel, 100, 1, &val);

	EXPECT_EQ(configBytes[100], 50);
}
