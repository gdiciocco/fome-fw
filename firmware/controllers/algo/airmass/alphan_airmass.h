#pragma once

#include "speed_density_base.h"

class AlphaNAirmass : public SpeedDensityBase {
public:
	explicit AlphaNAirmass(const ValueProvider3D* veTable = nullptr)
		: SpeedDensityBase(veTable) {}

	AirmassResult getAirmass(float rpm, bool postState) override;
	AirmassEvaluation evaluateAirmass(float rpm, AirmassDiagnostics* diagnostics = nullptr) const;
	float getVeImpl(float rpm, percent_t load) const override;
	AirmassEvaluation
	evaluateRawAirmass(const AirmassInputs& inputs, RawAirmassDiagnostics* diagnostics = nullptr) const;

private:
	float getDedicatedVeImpl(float rpm, float load) const override;
	AirmassEvaluation evaluateAirmass(float rpm, const DiagnosticsTarget& diagnostics) const;
};
