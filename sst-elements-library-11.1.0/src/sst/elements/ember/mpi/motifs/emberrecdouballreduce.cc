#include <sst_config.h>
#include "emberrecdouballreduce.h"

using namespace SST::Ember;

EmberRecDoubAllreduceGenerator::EmberRecDoubAllreduceGenerator(SST::ComponentId_t id, Params &params) 
: EmberRecDoubCollGenerator(id, params, "RecDoubAllreduce")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    uint32_t recvcount = (uint32_t)params.find("arg.count", 1);
    bool blocking = (bool)params.find("arg.blocking", true);
    uint dimensions = (uint)params.find("arg.dimensions", 1);
    int validate = (int)params.find("arg.validate", 0);
    uint ports = (uint)params.find("arg.ports", 1);
    bool sync = (bool)params.find("arg.sync", true);
    bool latency_optimal = (bool)params.find("arg.latency_optimal", false);
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

    m_validate = validate;
    m_recvcount = recvcount;
    m_data = NULL;
    if(m_validate){
        memSetBacked();
		m_data = (float*) memAlloc(sizeofDataType(FLOAT)*recvcount);
        m_data_validation_send = (float*) memAlloc(sizeofDataType(FLOAT)*recvcount);
        m_data_validation_recv = (float*) memAlloc(sizeofDataType(FLOAT)*recvcount);
        for(size_t i = 0; i < recvcount; i++){
            m_data[i] = rand() % 1024;
            m_data_validation_send[i] = m_data[i];
        }
        m_validation_reduce_executed = false;
    }
    m_allreduce = new RecDoubCollective(*this, dimensions, ports, recvcount, rank(), size(), GroupWorld, aggregation_cost_ns, 
                                        !blocking, sync, RECDOUB_ALLREDUCE, m_data, dimensions_sizes, latency_optimal);
}

EmberRecDoubAllreduceGenerator::~EmberRecDoubAllreduceGenerator()
{
    m_allreduce->printStats();
    delete m_allreduce;
}

bool EmberRecDoubAllreduceGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if (!m_allreduce->progress(evQ)){
        return false;
    }
    // Allreduce over, we can return true
    // If we need to validate, we run a standard allreduce
    if(m_validate){
        if(!m_validation_reduce_executed){
            enQ_allreduce(evQ, m_data_validation_send, m_data_validation_recv, m_recvcount, FLOAT, Hermes::MP::SUM, GroupWorld);
            m_validation_reduce_executed = true;
            return false;
        }else{
            for(size_t i = 0; i < m_recvcount; i++){
                if(m_data[i] != m_data_validation_recv[i]){
                    fprintf(stderr, "Validation error on rank %d at index %d (%d vs. %d)\n", rank(), i, m_data[i], m_data_validation_recv[i]);
                    exit(-1);
                }
            }
            printf("[Rank %d] Validation succeeded.\n", rank());
        }
    }
    return true;
}