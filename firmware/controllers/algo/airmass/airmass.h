#pragma once

#include "rusefi_types.h"
struct blend_table_s;
#include "engine_math.h"

class ValueProvider3D;

struct AirmassResult {
	mass_t CylinderAirmass = 0;
	percent_t EngineLoadPercent = 100;
};

struct AirmassModelBase {
	virtual AirmassResult getAirmass(float rpm, bool postState) = 0;
};

// Calculation snapshots contain values, never references to shared live diagnostics.
struct VeEvaluation {
	// Final VE in percent, after idle interpolation and correction multipliers.
	percent_t Ve = 0;
	float Load = 0;
	float IdleLoad = 0;
	BlendResult Blends[VE_BLEND_COUNT] = {};
	// Diagnostics were calculated and may be published even when Valid is false.
	bool HasValue = false;
	// calculateBlend retains its neutral fallback for unavailable correction inputs;
	// their validity must be checked separately before enabling a composite mode.
	bool Valid = false;
};

struct MapEvaluation {
	float Map = 0;
	float FallbackMap = 0;
	bool HasValue = false; // A fallback MAP diagnostic was evaluated for publication.
	bool Valid = false;
	bool UsesEstimate = false;
};

struct AirmassEvaluation {
	AirmassResult Result;
	VeEvaluation Ve;
	MapEvaluation Map;
	// Describes usable inputs/results separately from legacy numeric fault fallbacks.
	// Configuration readiness and composite fault policy are not evaluated here.
	bool Valid = false;
};

class AirmassVeModelBase : public AirmassModelBase {
public:
	explicit AirmassVeModelBase(const ValueProvider3D* veTable);

	// Retrieve the user-calibrated volumetric efficiency from the table
	float getVe(float rpm, percent_t load, bool postState) const;
	// Evaluation does not publish live diagnostics. Legacy warnings remain enabled.
	VeEvaluation evaluateVe(float rpm, percent_t load) const;
	static void publishVe(const VeEvaluation& evaluation);
	static void publishEvaluation(const AirmassEvaluation& evaluation);

	virtual float getVeImpl(float /*rpm*/, percent_t /*load*/) const;

protected:
	// Fill a model's snapshot directly without another VE snapshot on the stack.
	void evaluateVe(float rpm, percent_t load, VeEvaluation& evaluation) const;

private:
	const ValueProvider3D* const m_veTable;
};
