#include "phoenix/utils.hpp"

#include <cassert>

#include <pthread.h>
#include <sched.h>

namespace phoenix {

void setMaxThreadPriority()
{
    pthread_t thisThread = pthread_self();
    sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    assert(pthread_setschedparam(thisThread, SCHED_FIFO, &params));
}

}
