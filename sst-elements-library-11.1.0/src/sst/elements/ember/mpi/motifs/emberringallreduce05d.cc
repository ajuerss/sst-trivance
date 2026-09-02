#include <sst_config.h>
#include "emberringallreduce05d.h"

using namespace SST::Ember;

EmberRingAllreduce05DGenerator::EmberRingAllreduce05DGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RingAllreduce05D")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t count = (uint32_t)params.find("arg.count", 1);
    count_local = count;
    bool blocking = (bool)params.find("arg.blocking", true);

    m_allreduce = new EmberRingAllreduce05D(*this, count, rank(), size(), GroupWorld, aggregation_cost_ns, !blocking);
}

EmberRingAllreduce05DGenerator::~EmberRingAllreduce05DGenerator()
{
    m_allreduce->printStats();
    delete m_allreduce;
    stop_time_local = getCurrentSimTimeNano();
}

bool EmberRingAllreduce05DGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    enQ_getTime(evQ, &start_time_local);
    if (!m_allreduce->progress(evQ)) return false;
    return true;
}