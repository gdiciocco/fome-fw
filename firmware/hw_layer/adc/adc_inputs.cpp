/**
 * @file	adc_inputs.cpp
 * @brief	Low level ADC code
 *
 * rusEfi uses two ADC devices on the same 16 pins at the moment. Two ADC devices are used in orde to distinguish
 * between fast and slow devices. The idea is that but only having few channels in 'fast' mode we can sample those
 * faster?
 *
 * At the moment rusEfi does not allow to have more than 16 ADC channels combined. At the moment there is no flexibility
 * to use any ADC pins, only the hardcoded choice of 16 pins.
 *
 * Slow ADC group is used for IAT, CLT, AFR, VBATT etc - this one is currently sampled at 500Hz
 *
 * Fast ADC group is used for MAP, MAF HIP - this one is currently sampled at 10KHz
 *  We need frequent MAP for map_averaging.cpp
 *
 * 10KHz equals one measurement every 3.6 degrees at 6000 RPM
 *
 * @date Jan 14, 2013
 * @author Andrey Belomutskiy, (c) 2012-2020
 */

#include "pch.h"

float __attribute__((weak)) getAnalogInputDividerCoefficient(adc_channel_e) {
	return engineConfiguration->analogInputDividerCoefficient;
}

#if HAL_USE_ADC

#include "adc_subscription.h"
#include "protected_gpio.h"

static float mcuTemperature;

float getMCUInternalTemperature() {
	return mcuTemperature;
}

static uint32_t slowAdcCounter = 0;
static uint32_t slowAdcMaxUpdateGapUs = 0;
static uint32_t slowAdcReadFailureCount = 0;
static uint32_t slowAdcGapOver10msCount = 0;
static uint32_t slowAdcMaxConversionUs = 0;
static uint32_t slowAdcConversionOver10msCount = 0;
static efitick_t lastSlowAdcUpdateNt = 0;

uint32_t getSlowAdcMaxUpdateGapUs() {
	return slowAdcMaxUpdateGapUs;
}

uint32_t getSlowAdcReadFailureCount() {
	return slowAdcReadFailureCount;
}

uint32_t getSlowAdcGapOver10msCount() {
	return slowAdcGapOver10msCount;
}

uint32_t getSlowAdcMaxConversionUs() {
	return slowAdcMaxConversionUs;
}

uint32_t getSlowAdcConversionOver10msCount() {
	return slowAdcConversionOver10msCount;
}

void waitForSlowAdc() {
	// Wait for a few slow adc updates to happen
	while (slowAdcCounter < 5) {
		chThdSleepMilliseconds(1);
	}
}

void updateSlowAdc(efitick_t nowNt) {
	{
		ScopePerf perf(PE::AdcConversionSlow);

		auto conversionStartNt = getTimeNowNt();
		bool readSucceeded = readSlowAnalogInputs();
		auto conversionUs = NT2US(getTimeNowNt() - conversionStartNt);
		if (conversionUs > slowAdcMaxConversionUs) {
			slowAdcMaxConversionUs = conversionUs > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(conversionUs);
		}
		if (conversionUs > 10000) {
			slowAdcConversionOver10msCount++;
		}

		if (!readSucceeded) {
			slowAdcReadFailureCount++;
			return;
		}

		// Ask the port to sample the MCU temperature
		mcuTemperature = getMcuTemperature();
	}

	{
		ScopePerf perf(PE::AdcProcessSlow);
		if (lastSlowAdcUpdateNt != 0) {
			auto gapUs = NT2US(nowNt - lastSlowAdcUpdateNt);
			if (gapUs > 10000) {
				slowAdcGapOver10msCount++;
			}
			if (gapUs > slowAdcMaxUpdateGapUs) {
				slowAdcMaxUpdateGapUs = gapUs > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(gapUs);
			}
		}
		lastSlowAdcUpdateNt = nowNt;

		AdcSubscription::UpdateSubscribers(nowNt);

		protectedGpio_check(nowNt);
	}

	slowAdcCounter++;
}

#endif // HAL_USE_ADC
