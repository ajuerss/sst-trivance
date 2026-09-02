#include <sst_config.h>
#include "emberrandalltoall.h"

using namespace SST::Ember;

EmberRandAlltoallDGenerator::EmberRandAlltoallDGenerator(SST::ComponentId_t id, Params &params) 
: EmberHxMeshGenerator(id, params, "RandAlltoall")
{
    uint32_t count = (uint32_t)params.find("arg.count", 1);
    uint32_t blocksize = (uint32_t)params.find("arg.blocksize", 1);
    bool blocking = (bool)params.find("arg.blocking", true);
    bool deterministic = (bool)params.find("arg.deterministic", false);

    m_alltoall = new EmberRandAlltoall(*this, count, blocksize, rank(), size(), GroupWorld, !blocking, deterministic);
}

EmberRandAlltoallDGenerator::~EmberRandAlltoallDGenerator()
{
    delete m_alltoall;
}

bool EmberRandAlltoallDGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_alltoall->progress(evQ)) return false;
    return true;
}