#include <sst_config.h>
#include "emberringreducescatter1d.h"

using namespace SST::Ember;

EmberRingReduceScatter1DGenerator::EmberRingReduceScatter1DGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RingReduceScatter1D")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t recvcount = (uint32_t)params.find("arg.recvcount", 1);
    bool blocking = (bool)params.find("arg.blocking", true);

    m_reducescatter = new EmberRingAllreduce1D(*this, recvcount, rank(), size(), GroupWorld, aggregation_cost_ns, !blocking, RING_ALLGATHER);
}

EmberRingReduceScatter1DGenerator::~EmberRingReduceScatter1DGenerator()
{
    m_reducescatter->printStats();
    delete m_reducescatter;
}

bool EmberRingReduceScatter1DGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_reducescatter->progress(evQ)) return false;
    return true;
}