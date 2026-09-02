#include <sst_config.h>
#include "emberringallreduce2d.h"

using namespace SST::Ember;

EmberRingAllreduce2DGenerator::EmberRingAllreduce2DGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RingAllreduce2D")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t count = (uint32_t)params.find("arg.count", 1);
    bool blocking = (bool)params.find("arg.blocking", true);
    uint32_t concurrent = (uint32_t)params.find("arg.concurrent", 1);
    uint32_t px = (uint32_t)params.find("arg.px", 0);

    
    for (int i=0; i<concurrent; i++){
        m_allreduce.push_back(new EmberRingAllreduce2D(*this, count, rank(), size(), GroupWorld, aggregation_cost_ns, !blocking, px));
    }
}

EmberRingAllreduce2DGenerator::~EmberRingAllreduce2DGenerator()
{
    for (auto allreduce_ptr : m_allreduce) 
    {
        allreduce_ptr->printStats();
        delete allreduce_ptr;
    }
}

bool EmberRingAllreduce2DGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    bool allcompleted = true;
    for (auto allreduce_ptr : m_allreduce) {
        if (!allreduce_ptr->progress(evQ)) allcompleted = false;
    }
    return allcompleted;
}