/**
 * @file	scheduler.h
 *
 * @date October 1, 2020
 */
#include "pch.h"

#include "scheduler.h"

void action_s::execute() {
	efiAssertVoid(ObdCode::CUSTOM_ERR_ASSERT, m_callback != NULL, "callback==null1");
	m_callback(m_param);
}

schfunc_t action_s::getCallback() const {
	return m_callback;
}

void* action_s::getArgument() const {
	return m_param;
}

bool isScheduleBatchValid(const ScheduledAction* events, size_t count, efitick_t now) {
	if (!events || count == 0 || count > MaxScheduleBatchSize) {
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		// Comparing timestamps first avoids subtracting an arbitrary past timestamp.
		if (!events[i].action || (events[i].time >= now && events[i].time >= now + US2NT(MaximumScheduleDelayUs)) ||
			(i > 0 && events[i].time < events[i - 1].time)) {
			return false;
		}
	}

	return true;
}
