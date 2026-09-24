/**
 * @file perf_trace.cpp
 *
 * https://github.com/rusefi/rusefi/wiki/Developer-Performance-Tracing
 *
 * See JsonOutput.java in rusEfi console
 */

#include "pch.h"

#include <algorithm>
#include <cstring>

#ifndef ENABLE_PERF_TRACE
#error ENABLE_PERF_TRACE must be defined!
#endif

enum class EPhase : char {
	Start,
	End,
	InstantThread,
	InstantGlobal,
};

struct TraceEntry {
	PE Event;
	EPhase Phase;
	uint8_t IsrId;
	uint8_t ThreadId;
	uint32_t Timestamp;
};

// Ensure that the struct is the size we think it is - the binary layout is important
static_assert(sizeof(TraceEntry) == 8);

#define TRACE_BUFFER_LENGTH (BIG_BUFFER_SIZE / sizeof(TraceEntry))

// Ordinary traces stop when full; ADC gap traces overwrite the oldest records until triggered.
static BigBufferHandle s_traceBuffer;
static size_t s_nextIdx = 0;

static bool s_isTracing = false;
static bool s_isArmedForAdcGap = false;
static bool s_traceWrapped = false;

static void stopTrace() {
	s_isTracing = false;
}

static void perfEventImpl(PE event, EPhase phase) {
	// Bail if we aren't allowed to trace
	if constexpr (!ENABLE_PERF_TRACE) {
		return;
	}

	// Bail if we aren't tracing
	if (!s_isTracing || !s_traceBuffer) {
		return;
	}

	uint32_t timestamp = port_rt_get_counter_value();

	size_t idx;

	// Critical section: disable interrupts to reserve an index.
	// We could lock, but this gets called a LOT - so locks could
	// significantly alter the results of the measurement.
	// In addition, if we want to trace lock/unlock events, we can't
	// be locking ourselves from the trace functionality.
	{
		uint32_t prim = __get_PRIMASK();
		__disable_irq();

		idx = s_nextIdx;
		if (++s_nextIdx >= TRACE_BUFFER_LENGTH) {
			if (s_isArmedForAdcGap) {
				s_nextIdx = 0;
				s_traceWrapped = true;
			} else {
				stopTrace();
			}
		}

		// Restore previous interrupt state - don't restore if they weren't enabled
		if (!prim) {
			__enable_irq();
		}
	}

	// We can safely write data out of the lock, our spot is reserved
	volatile TraceEntry& entry = s_traceBuffer.get<TraceEntry>()[idx];

	entry.Event = event;
	entry.Phase = phase;
	// Get the current active interrupt - this is the "process ID"
	auto isr = static_cast<uint8_t>(SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk);

	// Get the current thread (if not interrupt) and use as the thread ID
	if (isr == 0) {
		entry.ThreadId = chThdGetSelfX()->threadId;
		entry.IsrId = 0;
	} else {
		entry.IsrId = isr - 16;

		// Interrupts have no thread - all are T0
		entry.ThreadId = 0;
	}

	entry.Timestamp = timestamp;
}

void perfEventBegin(PE event) {
	perfEventImpl(event, EPhase::Start);
}

void perfEventEnd(PE event) {
	perfEventImpl(event, EPhase::End);
}

void perfEventInstantGlobal(PE event) {
	perfEventImpl(event, EPhase::InstantGlobal);
}

static void prepareTrace() {
	stopTrace();
	s_traceBuffer = {};
	s_traceBuffer = getBigBuffer(BigBufferUser::PerfTrace);
	s_nextIdx = 0;
	s_traceWrapped = false;
	s_isArmedForAdcGap = false;
}

void perfTraceEnable() {
	prepareTrace();
	s_isTracing = static_cast<bool>(s_traceBuffer);
}

bool perfTraceArmSlowAdcGap() {
	prepareTrace();
	if (!s_traceBuffer) {
		return false;
	}

	std::memset(s_traceBuffer.get<uint8_t>(), 0, s_traceBuffer.size());
	s_isArmedForAdcGap = true;
	s_isTracing = true;
	return true;
}

void perfTraceFreezeOnSlowAdcGap() {
	if (s_isTracing && s_isArmedForAdcGap) {
		stopTrace();
		s_isArmedForAdcGap = false;
	}
}

bool perfTraceAdcGapPending() {
	return s_isArmedForAdcGap;
}

static inline uint32_t ticksToNs(uint32_t ticks) {
	const float ratio = 1e9 / STM32_SYSCLK;
	return (uint32_t)(ratio * ticks);
}

const BigBufferHandle perfTraceGetBuffer() {
	// stop tracing if you try to get the buffer early
	stopTrace();
	s_isArmedForAdcGap = false;

	if (!s_traceBuffer) {
		return {};
	}

	auto entries = s_traceBuffer.get<TraceEntry>();
	if (s_traceWrapped) {
		// The oldest record is at s_nextIdx. Return records in time order for the
		// existing trace decoder, which expects a linear buffer.
		std::rotate(entries, entries + s_nextIdx, entries + TRACE_BUFFER_LENGTH);
	}
	const size_t validEntries = s_traceWrapped ? TRACE_BUFFER_LENGTH : s_nextIdx;
	if (validEntries == 0) {
		return efi::move(s_traceBuffer);
	}

	auto timestampOffset = entries[0].Timestamp;

	for (size_t i = 0; i < validEntries; i++) {
		auto& entry = entries[i];

		// Remove offset and convert ticks -> nanoseconds
		// (first entry will be zero timestamp)
		entry.Timestamp = ticksToNs(entry.Timestamp - timestampOffset);
	}
	// Mark the unused tail so Entry.parseBuffer stops after the valid records.
	if (validEntries < TRACE_BUFFER_LENGTH) {
		std::memset(entries + validEntries, 0, (TRACE_BUFFER_LENGTH - validEntries) * sizeof(TraceEntry));
	}

	// transfer ownership of the buffer to the caller
	return efi::move(s_traceBuffer);
}
