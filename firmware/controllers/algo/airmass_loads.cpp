#include "pch.h"
#include "airmass_loads.h"
#include "airmass.h"
#include "gppwm_channel.h"
#include "fuel_math.h"
#include "closed_loop_fuel.h"

namespace {
bool revisedModel() {
	const auto mode = engineConfiguration->fuelAlgorithm;
	return mode == LM_SPEED_DENSITY || mode == LM_ALPHA_N || mode == LM_REAL_MAF || mode == LM_SD_ALPHA_N;
}
load_override_e selector(AirmassConsumer consumer, size_t index) {
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
			return config->injectionPhaseLoadSource;
		case AirmassConsumer::FuelTrim:
			return config->fuelTrimLoadSource[index];
		case AirmassConsumer::IgnitionTrim:
			return config->ignitionTrimLoadSource[index];
		case AirmassConsumer::Stft:
			return config->stftLoadSource;
		case AirmassConsumer::Staging:
			return config->stagingLoadSource;
		case AirmassConsumer::LambdaDeviation:
			return config->lambdaDeviationLoadSource;
		case AirmassConsumer::LambdaMonitor:
			return config->lambdaMonitorLoadSource;
		case AirmassConsumer::TrailingSpark:
			return config->trailingSparkLoadSource;
		case AirmassConsumer::IgnitionIat:
			return config->ignitionIatLoadSource;
		case AirmassConsumer::KnockRetard:
			return config->knockRetardLoadSource;
		case AirmassConsumer::KnockGain:
			return config->knockGainLoadSource[index];
		case AirmassConsumer::HpfpTarget:
			return config->hpfpTargetLoadSource;
		default:
			return static_cast<load_override_e>(255);
	}
}
bool cylinderConsumer(AirmassConsumer consumer) {
	return consumer == AirmassConsumer::FuelTrim || consumer == AirmassConsumer::IgnitionTrim ||
		   consumer == AirmassConsumer::KnockGain;
}
float fromSnapshot(const AirmassLoadSnapshot& s, load_override_e source);
bool active(AirmassConsumer consumer, const AirmassInputs& inputs, const AirmassLoadSnapshot& snapshot) {
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
		case AirmassConsumer::FuelTrim:
			return engineConfiguration->isInjectionEnabled;
		case AirmassConsumer::Stft: {
#if EFI_SHAFT_POSITION_INPUT
			const auto clt = Sensor::get(SensorType::Clt);
			const auto& stft = engineConfiguration->stft;
			return engineConfiguration->fuelClosedLoopCorrectionEnabled &&
				   engine->rpmCalculator.isRunning() && clt && clt.Value >= stft.minClt &&
				   engine->fuelComputer.running.timeSinceCrankingInSecs >= stft.startupDelay;
#else
			return false;
#endif
		}
		case AirmassConsumer::Staging:
			return engineConfiguration->isInjectionEnabled && engineConfiguration->enableStagedInjection;
		case AirmassConsumer::LambdaDeviation:
		case AirmassConsumer::LambdaMonitor: {
			if (!engineConfiguration->lambdaProtectionEnable) {
				return false;
			}
			if (consumer == AirmassConsumer::LambdaMonitor && engine->lambdaMonitor.isCut()) {
				return true;
			}
			if (inputs.Rpm < engineConfiguration->lambdaProtectionMinRpm || !inputs.Tps ||
				inputs.Tps.Value <= engineConfiguration->lambdaProtectionMinTps ||
				engine->module<DfcoController>()->getTimeSinceCut() < engineConfiguration->noFuelTrimAfterDfcoTime ||
				engine->module<LimpManager>()->getTimeSinceAnyCut() < 2) {
				return false;
			}
			if (consumer == AirmassConsumer::LambdaMonitor) {
				return true;
			}
			const float load = fromSnapshot(snapshot, config->lambdaMonitorLoadSource);
			return std::isfinite(load) && load >= engineConfiguration->lambdaProtectionMinLoad &&
				   Sensor::get(SensorType::Lambda1).Valid;
		}
		case AirmassConsumer::IgnitionTrim:
			return engineConfiguration->isIgnitionEnabled;
		case AirmassConsumer::IgnitionIat:
			return engineConfiguration->isIgnitionEnabled && inputs.Iat &&
				   engineConfiguration->timingMode == TM_DYNAMIC &&
				   (!engine->rpmCalculator.isCranking() || engineConfiguration->useAdvanceCorrectionsForCranking);
		case AirmassConsumer::TrailingSpark:
			return engineConfiguration->isIgnitionEnabled && engineConfiguration->enableTrailingSparks;
		case AirmassConsumer::KnockGain:
		case AirmassConsumer::KnockRetard:
			return engineConfiguration->enableSoftwareKnock && engine->rpmCalculator.isRunning();
		case AirmassConsumer::HpfpTarget:
			return inputs.Rpm >= 60 && enginePins.hpfpValve.isInitialized() && engineConfiguration->hpfpCamLobes &&
				   engineConfiguration->hpfpPumpVolume > 0;
		default:
			return false;
	}
}
bool lambdaTargetRequired(const AirmassInputs& inputs, const AirmassLoadSnapshot& snapshot) {
	if (engineConfiguration->isInjectionEnabled || active(AirmassConsumer::LambdaDeviation, inputs, snapshot)) {
		return true;
	}
	if (active(AirmassConsumer::Stft, inputs, snapshot)) {
		for (const auto sensor : {SensorType::Lambda1, SensorType::Lambda2, SensorType::Lambda3, SensorType::Lambda4}) {
			if (Sensor::get(sensor).Valid && shouldUpdateCorrection(sensor)) {
				return true;
			}
		}
	}
	return false;
}
float fromSnapshot(const AirmassLoadSnapshot& s, load_override_e source) {
	if (source > AFR_EffectiveMAP || !(s.ValidSources & (1u << source))) {
		return NAN;
	}
	switch (source) {
		case AFR_None:
			return s.Native;
		case AFR_MAP:
			return s.Map;
		case AFR_Tps:
			return s.Tps;
		case AFR_AccPedal:
			return s.Pedal;
		case AFR_CylFilling:
			return s.Filling;
		case AFR_EffectiveMAP:
			return s.EffectiveMap;
		default:
			return NAN;
	}
}
bool validLoadChannel(const AirmassLoadSnapshot& snapshot, gppwm_channel_e channel) {
	switch (channel) {
		case GPPWM_Map:
			return std::isfinite(fromSnapshot(snapshot, AFR_MAP));
		case GPPWM_EffectiveMap:
			return std::isfinite(fromSnapshot(snapshot, AFR_EffectiveMAP));
		case GPPWM_Tps:
			return std::isfinite(fromSnapshot(snapshot, AFR_Tps));
		case GPPWM_AccelPedal:
			return std::isfinite(fromSnapshot(snapshot, AFR_AccPedal));
		case GPPWM_FuelLoad:
			return std::isfinite(fromSnapshot(snapshot, AFR_None));
		case GPPWM_IgnLoad:
			return std::isfinite(fromSnapshot(snapshot, engineConfiguration->ignOverrideMode));
		// Other channels keep the actuator's existing fallback policy. This pass
		// validates load dependencies without recursively evaluating GPPWM outputs.
		default:
			return true;
	}
}
bool indirectLoadsValid(const AirmassLoadSnapshot& snapshot, const AirmassInputs& inputs) {
	if (engineConfiguration->isIgnitionEnabled && engineConfiguration->timingMode == TM_DYNAMIC &&
		(!engine->rpmCalculator.isCranking() || !engineConfiguration->useSeparateAdvanceForCranking)) {
		for (const auto& blend : config->ignBlends) {
			if (blend.blendParameter != GPPWM_Zero && (!validLoadChannel(snapshot, blend.blendParameter) ||
													   !validLoadChannel(snapshot, blend.yAxisOverride))) {
				return false;
			}
		}
	}
	const auto clt = Sensor::get(SensorType::Clt);
	for (size_t cam = 0; cam < CAMS_PER_BANK; cam++) {
		bool hasOutput = false;
		for (size_t index = cam; index < efi::size(engineConfiguration->vvtPins); index += CAMS_PER_BANK) {
			hasOutput |= isBrainPinValid(engineConfiguration->vvtPins[index]);
		}
		if (hasOutput && inputs.Rpm > engineConfiguration->vvtControlMinRpm &&
			inputs.Rpm > engineConfiguration->cranking.rpm && clt &&
			clt.Value > engineConfiguration->vvtControlMinClt &&
			engine->rpmCalculator.getSecondsSinceEngineStart(getTimeNowNt()) >
					engineConfiguration->vvtActivationDelayMs / MS_PER_SECOND) {
			const auto source = cam == 0 ? engineConfiguration->vvtIntakeYAxisOverride
										 : engineConfiguration->vvtExhaustYAxisOverride;
			if (!validLoadChannel(snapshot, source == GPPWM_Zero ? GPPWM_FuelLoad : source)) {
				return false;
			}
		}
	}
	for (const auto& pwm : engineConfiguration->gppwm) {
		if (isBrainPinValid(pwm.pin) &&
			(!validLoadChannel(snapshot, pwm.rpmAxis) || !validLoadChannel(snapshot, pwm.loadAxis))) {
			return false;
		}
	}
	const bool acActive = engine->module<AcController>()->isAcEnabled();
	if (clt && engineConfiguration->fan1UsePwmMode && isBrainPinValid(engineConfiguration->fanPin) &&
		engine->module<FanControl1>()->m_state &&
		!validLoadChannel(snapshot, acActive ? config->fan1PwmAcOnXAxis : engineConfiguration->fan1PwmXAxis)) {
		return false;
	}
	if (clt && engineConfiguration->fan2UsePwmMode && isBrainPinValid(engineConfiguration->fan2Pin) &&
		engine->module<FanControl2>()->m_state &&
		!validLoadChannel(snapshot, acActive ? config->fan2PwmAcOnXAxis : engineConfiguration->fan2PwmXAxis)) {
		return false;
	}

	bool hasBoostOutput = isBrainPinValid(engineConfiguration->boostControlPin);
	for (const auto function : engineConfiguration->etbFunctions) {
		hasBoostOutput |= function == DC_Wastegate;
	}
	if (engineConfiguration->isBoostControlEnabled && hasBoostOutput &&
		inputs.Rpm > engineConfiguration->boostControlMinRpm &&
		inputs.Tps.value_or(0) >= engineConfiguration->boostControlMinTps &&
		inputs.MeasuredMap.value_or(0) >= engineConfiguration->boostControlMinMap) {
		// Boost's legacy TPS axes use driver intent (pedal when ETB is configured).
		const auto boostAxisValid = [&](gppwm_channel_e source) {
			return source == GPPWM_Tps ? inputs.DriverThrottleIntent.Valid : validLoadChannel(snapshot, source);
		};
		const auto boostTableValid = [&](bool closedLoop) {
			const auto x =
					closedLoop ? engineConfiguration->boostClosedLoopXAxis : engineConfiguration->boostOpenLoopXAxis;
			const auto y =
					closedLoop ? engineConfiguration->boostClosedLoopYAxis : engineConfiguration->boostOpenLoopYAxis;
			if (!boostAxisValid(x) || !boostAxisValid(y)) {
				return false;
			}
			const auto& blends = closedLoop ? config->boostClosedLoopBlends : config->boostOpenLoopBlends;
			const auto& xs = closedLoop ? config->boostClosedLoopBlendXAxis : config->boostOpenLoopBlendXAxis;
			for (size_t i = 0; i < efi::size(blends); i++) {
				const auto& blend = blends[i];
				if (blend.blendParameter != GPPWM_Zero && (!boostAxisValid(xs[i] == GPPWM_Zero ? x : xs[i]) ||
														   !validLoadChannel(snapshot, blend.blendParameter) ||
														   !validLoadChannel(snapshot, blend.yAxisOverride))) {
					return false;
				}
			}
			return true;
		};
		if (!boostTableValid(false) || (engineConfiguration->boostType == CLOSED_LOOP && !boostTableValid(true))) {
			return false;
		}
	}
	if (engineConfiguration->enableTorqueModel || engineConfiguration->enableTractionControl) {
		if (engineConfiguration->enableTorqueModel &&
			!validLoadChannel(snapshot, engineConfiguration->torqueModel.torqueLossLoadAxis)) {
			return false;
		}
		for (const auto& limiter : engineConfiguration->torqueLimiters) {
			if (limiter.enable &&
				(!validLoadChannel(snapshot, limiter.xAxis) || !validLoadChannel(snapshot, limiter.yAxis))) {
				return false;
			}
		}
		if (engineConfiguration->enableTractionControl && engine->tractionController.getSlip() &&
			engine->module<GearDetector>()->getTotalRatioInCurrentGear() &&
			!validLoadChannel(snapshot, engineConfiguration->tractionControl.slipTargetYAxis)) {
			return false;
		}
	}
	return true;
}
void publishCursor(AirmassConsumer consumer, size_t index, float value) {
	// Never cast NaN into the packed representation. Validity remains in the
	// full precision snapshot and injection state, not encoded as a fake load.
	value = std::isfinite(value) ? clampF(-3276, value, 3276) : 0;
	auto& out = engine->outputChannels;
	switch (consumer) {
		case AirmassConsumer::InjectionPhase:
			out.injectionPhaseLoad = value;
			break;
		case AirmassConsumer::FuelTrim:
			out.fuelTrimLoad[index] = value;
			break;
		case AirmassConsumer::IgnitionTrim:
			out.ignitionTrimLoad[index] = value;
			break;
		case AirmassConsumer::Stft:
			out.stftLoad = value;
			break;
		case AirmassConsumer::Staging:
			out.stagingLoad = value;
			break;
		case AirmassConsumer::LambdaDeviation:
			out.lambdaDeviationLoad = value;
			break;
		case AirmassConsumer::LambdaMonitor:
			out.lambdaMonitorLoad = value;
			break;
		case AirmassConsumer::TrailingSpark:
			out.trailingSparkLoad = value;
			break;
		case AirmassConsumer::IgnitionIat:
			out.ignitionIatLoad = value;
			break;
		case AirmassConsumer::KnockRetard:
			out.knockRetardLoad = value;
			break;
		case AirmassConsumer::KnockGain:
			out.knockGainLoad[index] = value;
			break;
		case AirmassConsumer::HpfpTarget:
			out.hpfpTargetLoad = value;
			break;
		default:
			break;
	}
}
} // namespace

bool processAirmassConsumerLoads(const AirmassInputs& inputs, mass_t mass, bool publish) {
	AirmassLoadSnapshot snapshot;
	snapshot.Native = inputs.NativeLoad;
	snapshot.Map = inputs.MeasuredMap.value_or(NAN);
	snapshot.EffectiveMap = inputs.EffectiveMap.Valid ? inputs.EffectiveMap.Map : NAN;
	snapshot.Tps = inputs.Tps.value_or(NAN);
	snapshot.Pedal = inputs.Pedal.value_or(NAN);
	snapshot.Native = resolveAirmassLoad(inputs, mass, AFR_None).Value;
	snapshot.Filling = resolveAirmassLoad(inputs, mass, AFR_CylFilling).Value;
	snapshot.UsesEstimate = inputs.EffectiveMap.UsesEstimate;
	snapshot.ConfigurationVersion = inputs.ConfigurationVersion;
	for (uint8_t i = 0; i <= AFR_EffectiveMAP; i++) {
		if (resolveAirmassLoad(inputs, mass, static_cast<load_override_e>(i)).Valid) {
			snapshot.ValidSources |= 1u << i;
		}
	}
	bool valid = indirectLoadsValid(snapshot, inputs);
	for (size_t i = 0; i < static_cast<size_t>(AirmassConsumer::Count); i++) {
		const auto consumer = static_cast<AirmassConsumer>(i);
		const size_t count = cylinderConsumer(consumer) ? engine->engineState.cylinderCount : 1;
		for (size_t cylinder = 0; cylinder < count && cylinder < MAX_CYLINDER_COUNT; cylinder++) {
			const float value = fromSnapshot(snapshot, selector(consumer, cylinder));
			if (active(consumer, inputs, snapshot) && !std::isfinite(value)) {
				valid = false;
			}
		}
	}
	// STFT and protection can read the target even when injection is disabled.
	snapshot.LambdaTargetRequired = lambdaTargetRequired(inputs, snapshot);
	if (snapshot.LambdaTargetRequired && !std::isfinite(fromSnapshot(snapshot, inputs.LambdaOverride))) {
		valid = false;
	}
	if (engineConfiguration->isIgnitionEnabled && !std::isfinite(fromSnapshot(snapshot, inputs.IgnitionOverride))) {
		valid = false;
	}
	snapshot.Valid = valid;
	if (publish) {
		chibios_rt::CriticalSectionLocker csl;
		if (!inputs.HasPublicationContext || inputs.PublicationEpoch != engine->airmassInjectionState.publicationEpoch() ||
			inputs.ConfigurationVersion != engine->getGlobalConfigurationVersion() ||
			inputs.ActiveStrategy != engineConfiguration->fuelAlgorithm) {
			return false;
		}
		engine->engineState.airmassLoads = snapshot;
		for (size_t i = 0; i < static_cast<size_t>(AirmassConsumer::Count); i++) {
			const auto consumer = static_cast<AirmassConsumer>(i);
			const size_t count = cylinderConsumer(consumer) ? engine->engineState.cylinderCount : 1;
			for (size_t cylinder = 0; cylinder < count && cylinder < MAX_CYLINDER_COUNT; cylinder++) {
				publishCursor(consumer, cylinder, valid ? fromSnapshot(snapshot, selector(consumer, cylinder)) : NAN);
			}
		}
	}
	return valid;
}

bool isAirmassLambdaTargetRequired() {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	// An invalid publication must never be interpreted as permission to skip validation.
	return !snapshot.Valid || snapshot.ConfigurationVersion != engine->getGlobalConfigurationVersion() ||
		   snapshot.LambdaTargetRequired;
}

float getAirmassSelectedLoad(load_override_e source, float legacyDefault) {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	if (snapshot.Valid && snapshot.ConfigurationVersion == engine->getGlobalConfigurationVersion()) {
		return fromSnapshot(snapshot, source);
	}
	if (revisedModel()) {
		return NAN;
	}
	// Lua/mock/legacy external model callers have no capture owner. Explicit
	// physical sources still preserve validity, rather than changing coordinate.
	switch (source) {
		case AFR_None:
			return legacyDefault;
		case AFR_MAP:
			return Sensor::get(SensorType::Map).value_or(NAN);
		case AFR_Tps:
			return Sensor::get(SensorType::Tps1).value_or(NAN);
		case AFR_AccPedal:
			return Sensor::get(SensorType::AcceleratorPedal).value_or(NAN);
		case AFR_CylFilling:
			return engine->fuelComputer.normalizedCylinderFilling;
		case AFR_EffectiveMAP:
			return getEffectiveAirmassMap().value_or(NAN);
		default:
			return NAN;
	}
}
float getAirmassConsumerLoad(AirmassConsumer consumer, size_t index) {
	chibios_rt::CriticalSectionLocker csl;
	if (index >= MAX_CYLINDER_COUNT) {
		return NAN;
	}
	const float value = getAirmassSelectedLoad(selector(consumer, index), getFuelingLoad());
	publishCursor(consumer, index, value);
	return value;
}
expected<float> getEffectiveAirmassMap() {
	chibios_rt::CriticalSectionLocker csl;
	const auto& snapshot = engine->engineState.airmassLoads;
	if (!snapshot.Valid || snapshot.ConfigurationVersion != engine->getGlobalConfigurationVersion()) {
		return unexpected;
	}
	const float value = fromSnapshot(snapshot, AFR_EffectiveMAP);
	return std::isfinite(value) ? expected<float>(value) : unexpected;
}
void invalidateAirmassLoads(bool engineStopped) {
	chibios_rt::CriticalSectionLocker csl;
	auto& state = engine->engineState;
	state.airmassLoads.Valid = false;
	state.airmassCalculationValid = false;
	state.injectionDuration = 0;
	state.injectionDurationStage2 = 0;
	state.baseFuel = 0;
	for (auto& cylinder : engine->cylinders) {
		cylinder.setInjectionMass(0);
	}
	state.veAnalyzeSessionInvalid = !engineStopped;
	if (engineStopped) {
		state.veAnalyzeSessionStarted = false;
	}
	state.veAnalyzeEndpoint = 0;
	engine->outputChannels.blendedVeAnalyzeEndpoint = 0;
}
void updateBlendedVeAnalyzeQualification(float rpm) {
	auto& state = engine->engineState;
	if (!(rpm > 0)) {
		// A sensor zero/NaN is not evidence of a physical stop. Only the engine
		// stop transition (or a configuration write at a confirmed stop) can
		// rearm a session after a tune change or airmass fault.
		engine->outputChannels.blendedVeAnalyzeEndpoint = 0;
		return;
	}
	if (!state.veAnalyzeSessionStarted) {
		state.veAnalyzeSessionStarted = true;
		const auto endpoint = config->airmassBlendTable[0][0];
		bool uniform = endpoint == 0 || endpoint == 100;
		for (const auto& row : config->airmassBlendTable) {
			for (auto value : row) {
				uniform &= value == endpoint;
			}
		}
		if (!state.veAnalyzeSessionInvalid && engineConfiguration->fuelAlgorithm == LM_SD_ALPHA_N &&
			!engineConfiguration->useSeparateVeForIdle && uniform) {
			state.veAnalyzeEndpoint = endpoint == 0 ? 1 : 2;
		}
	}
	engine->outputChannels.blendedVeAnalyzeEndpoint =
			state.veAnalyzeSessionInvalid || engine->fuelComputer.running.timeSinceCrankingInSecs < 10
					? 0
					: state.veAnalyzeEndpoint;
}
