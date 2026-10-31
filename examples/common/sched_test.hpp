#ifndef _MOS_SCHED_TEST_
#define _MOS_SCHED_TEST_

#include "kernel.hpp"

namespace MOS::Examples::Test
{
	using namespace Kernel;

	// Periodic workload used to observe the scheduler in Renode or on target.
	// It is only compiled when MOS_SCHED_DEMO is defined.
	template <uint32_t PERIOD_MS, uint32_t WORK_MS>
	inline void periodic_job()
	{
		constexpr uint32_t WORK = WORK_MS * MOS_CONF_SYSTICK / 1000;

		const char* name = Task::current()->get_name();
		uint32_t count   = 0;

		while (true) {
			const auto start = Global::os_ticks;
			while ((int32_t) (Global::os_ticks - (start + WORK)) < 0) {
				MOS_NOP();
			}

			count += 1;
			LOG(
			    "[%s] job %u @%u (dl=%u)",
			    name, count, Global::os_ticks,
			    Task::current()->get_dl_abs()
			);

			Task::periodic_wait();
		}
	}

	inline void sched_monitor()
	{
		while (true) {
			Task::delay(2_s);
			LOG(
			    "[sched] %s load=%u%% misses=%u",
			    policy_name(DEFAULT_POLICY),
			    Task::periodic_load() / 10,
			    Task::missed_deadlines()
			);
		}
	}

	inline void SchedTest()
	{
		LOG("=== Scheduling policy: %s ===", policy_name(DEFAULT_POLICY));

		Task::create_periodic(
		    periodic_job<200, 10>, nullptr, 0, 200_ms, 200_ms, "rt/200", 10_ms
		);
		Task::create_periodic(
		    periodic_job<400, 10>, nullptr, 0, 400_ms, 400_ms, "rt/400", 10_ms
		);
		Task::create_periodic(
		    periodic_job<800, 10>, nullptr, 0, 800_ms, 800_ms, "rt/800", 10_ms
		);

		Task::create(sched_monitor, nullptr, 1, "sched/mon");
	}
}

#endif
