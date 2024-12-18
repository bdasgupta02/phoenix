#include "phoenix/utils.hpp"

#include <cassert>

#include <pthread.h>
#include <sched.h>

namespace phoenix {

void setCpu(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    assert(result == 0);
}

void setMaxThreadPriority()
{
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch_params);
    assert(result == 0);
}

}
