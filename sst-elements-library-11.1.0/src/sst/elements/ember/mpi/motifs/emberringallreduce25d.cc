#include <sst_config.h>
#include "emberringallreduce25d.h"

using namespace SST::Ember;

EmberRingAllreduce25DGenerator::EmberRingAllreduce25DGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RingAllreduce25D")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t count = (uint32_t)params.find("arg.count", 1);
    count_local = count;
    bool blocking = (bool)params.find("arg.blocking", true);
    uint32_t concurrent = (uint32_t)params.find("arg.concurrent", 1);
    uint32_t px = (uint32_t)params.find("arg.px", 0);

    
    for (int i=0; i<concurrent; i++){
        m_allreduce.push_back(new EmberRingAllreduce25D(*this, count, rank(), size(), GroupWorld, aggregation_cost_ns, !blocking, px));
    }
}

EmberRingAllreduce25DGenerator::~EmberRingAllreduce25DGenerator()
{
    for (auto allreduce_ptr : m_allreduce) 
    {
        allreduce_ptr->printStats();
        delete allreduce_ptr;
    }
}

bool EmberRingAllreduce25DGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    bool allcompleted = true;
    //enQ_getTime(evQ, &start_time_local);
    for (auto allreduce_ptr : m_allreduce) {
        if (!allreduce_ptr->progress(evQ)) allcompleted = false;
    }
    //enQ_getTime(evQ, &stop_time_local);
    //printf("Size %d - Start %" PRIu64 " - Stop %" PRIu64 " - Diff %" PRIu64 " - Count %d\n", size(), start_time_local, stop_time_local, stop_time_local - start_time_local, count_local);
    return allcompleted;
}