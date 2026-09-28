#pragma once

#include "rusefi_types.h"
struct blend_table_s;
#include "engine_math.h"

class ValueProvider3D;

// Queries must not change engine fault state. The live fuel owner and config
// application use the fatal validator; model evaluations use the pure predicate.
bool isAirmassConfigurationValid();
bool validateAirmassConfiguration();

struct AirmassResult {
	mass_t CylinderAirmass = 0;
	percent_t EngineLoadPercent = 100;
};

struct AirmassModelBase {
	virtual AirmassResult getAirmass(float rpm, bool postState) = 0;
};

struct VeEvaluation {
	percent_t Ve = 0;
	// Missing correction inputs retain calculateBlend's neutral fallback. Validate
	// those separately before enabling a composite mode.
	bool Valid = false;
};

// Optional caller-owned diagnostics contain values, never references to live state.
// On reuse, absent payloads are retained but HasValue and Valid are cleared.
struct VeDiagnostics {
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
	// Describes usable inputs/results separately from legacy numeric fault fallbacks.
	// Table compatibility is checked; calibration quality and composite fault
	// policy are not evaluated here.
	bool Valid = false;
};

struct AirmassDiagnostics {
	VeDiagnostics Ve;
	MapEvaluation Map;
};

class AirmassVeModelBase : public AirmassModelBase {
public:
	explicit AirmassVeModelBase(const ValueProvider3D* veTable);

	// Retrieve the user-calibrated volumetric efficiency from the table
	float getVe(float rpm, percent_t load, bool postState) const;
	// Evaluation does not publish live diagnostics. Legacy warnings remain enabled.
	VeEvaluation evaluateVe(float rpm, percent_t load, VeDiagnostics* diagnostics = nullptr) const;
	static void publishVe(const VeDiagnostics& diagnostics);
	static void publishEvaluation(const AirmassDiagnostics& diagnostics);

	virtual float getVeImpl(float /*rpm*/, percent_t /*load*/) const;

protected:
	// Legacy wrappers select live delivery. Public evaluations only select optional
	// capture, so neither dry nor live calculations need a diagnostics array local.
	class DiagnosticsTarget {
	public:
		explicit DiagnosticsTarget(bool postState);
		explicit DiagnosticsTarget(VeDiagnostics* diagnostics);
		explicit DiagnosticsTarget(AirmassDiagnostics* diagnostics);

		void blend(size_t index, const BlendResult& result) const;
		void ve(const VeEvaluation& result, float load, float idleLoad) const;
		void map(const MapEvaluation& result) const;

	private:
		VeDiagnostics* m_ve = nullptr;
		MapEvaluation* m_map = nullptr;
		bool m_postState = false;
	};

	VeEvaluation evaluateVe(float rpm, percent_t load, const DiagnosticsTarget& diagnostics) const;

private:
	const ValueProvider3D* const m_veTable;
};
