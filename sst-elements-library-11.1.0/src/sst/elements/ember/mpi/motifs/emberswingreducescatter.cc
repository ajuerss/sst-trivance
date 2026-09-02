#include <sst_config.h>
#include "emberswingreducescatter.h"

using namespace SST::Ember;

EmberSwingReduceScatterGenerator::EmberSwingReduceScatterGenerator(SST::ComponentId_t id, Params &params) 
: EmberSwingCollGenerator(id, params, "SwingReduceScatter")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t recvcount = (uint32_t)params.find("arg.count", 1);
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

    m_reducescatter = new SwingCollective(*this, dimensions, ports, recvcount, rank(), size(), GroupWorld, aggregation_cost_ns, 
                                          !blocking, sync, SWING_REDUCE_SCATTER, NULL, dimensions_sizes);
}

EmberSwingReduceScatterGenerator::~EmberSwingReduceScatterGenerator()
{
    m_reducescatter->printStats();
    delete m_reducescatter;
}

bool EmberSwingReduceScatterGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_reducescatter->progress(evQ)) return false;
    return true;
}