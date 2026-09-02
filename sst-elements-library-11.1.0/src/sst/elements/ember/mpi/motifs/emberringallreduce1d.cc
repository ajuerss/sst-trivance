#include <sst_config.h>
#include "emberringallreduce1d.h"

using namespace SST::Ember;

EmberRingAllreduce1DGenerator::EmberRingAllreduce1DGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RingAllreduce1D")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t count = (uint32_t)params.find("arg.count", 1);
    bool blocking = (bool)params.find("arg.blocking", true);

    m_allreduce = new EmberRingAllreduce1D(*this, count, rank(), size(), GroupWorld, aggregation_cost_ns, !blocking);
}

EmberRingAllreduce1DGenerator::~EmberRingAllreduce1DGenerator()
{
    m_allreduce->printStats();
    delete m_allreduce;
}

bool EmberRingAllreduce1DGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_allreduce->progress(evQ)) return false;
    return true;
}