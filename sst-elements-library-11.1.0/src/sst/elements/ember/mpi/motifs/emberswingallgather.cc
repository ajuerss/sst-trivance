#include <sst_config.h>
#include "emberswingallgather.h"

using namespace SST::Ember;

EmberSwingAllgatherGenerator::EmberSwingAllgatherGenerator(SST::ComponentId_t id, Params &params) 
: EmberSwingCollGenerator(id, params, "SwingAllgather")
{
    uint32_t recvcount = (uint32_t)params.find("arg.recvcount", 1);
    bool blocking = (bool)params.find("arg.blocking", true);
    uint dimensions = (uint)params.find("arg.dimensions", 1);
    uint ports = (uint)params.find("arg.ports", 1);
    bool sync = (bool)params.find("arg.sync", true);
    std::string dimensions_sizes_s = params.find<std::string>("arg.dimensions_sizes", ""); 
    uint* dimensions_sizes = NULL;

    // Split the dimensions_sizes string into the value of each dimensions
    if(dimensions_sizes_s != ""){
        dimensions_sizes = (uint*) malloc(sizeof(uint)*dimensions);
        std::string tmp; 
        std::stringstream ss(dimensions_sizes_s);
        uint i = 0;
        while(getline(ss, tmp, ',')){
            if(i >= dimensions){
                std::cerr << "Too many dimensions sizes specified" << std::endl;
            }
            size_t index = dimensions - i - 1; // Dimensions are numbered in the reverse order
            dimensions_sizes[index] = std::stoul(tmp);
            ++i;
        }
        std::cout << "Dimensions: ";
        for(int i = dimensions - 1; i >= 0; i--){
            std::cout << dimensions_sizes[i] << " ";
        }
        std::cout << std::endl;
    }

    m_allgather = new SwingCollective(*this, dimensions, ports, recvcount, rank(), size(), GroupWorld, 0, 
                                      !blocking, sync, SWING_ALLGATHER, NULL, dimensions_sizes);
}

EmberSwingAllgatherGenerator::~EmberSwingAllgatherGenerator()
{
    m_allgather->printStats();
    delete m_allgather;
}

bool EmberSwingAllgatherGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_allgather->progress(evQ)) return false;
    return true;
}