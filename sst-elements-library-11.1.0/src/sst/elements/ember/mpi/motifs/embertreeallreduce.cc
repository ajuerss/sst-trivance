#include <sst_config.h>
#include "embertreeallreduce.h"
#include <math.h>

#define DEBUG

#ifdef DEBUG
#define DPRINTF(...) printf(__VA_ARGS__)
#else
#define DPRINTF(...) 
#endif


using namespace SST::Ember;

EmberTreeAllreduceGenerator::EmberTreeAllreduceGenerator(SST::ComponentId_t id, Params &params) 
: EmberMessagePassingGenerator(id, params, "TreeAllreduce")
{
    double aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    m_recvcount = (uint32_t)params.find("arg.count", 1);
    m_validate = (int)params.find("arg.validate", 0);
    uint ports = (uint)params.find("arg.ports", 1);

    if(m_recvcount >= ports){
        m_recvcount /= ports; // Emulate multiported
    }

    m_data = NULL;
    m_tmp_data = NULL;
    if(m_validate){
        memSetBacked();
		m_data = (float*) memAlloc(sizeofDataType(FLOAT)*m_recvcount);
        m_tmp_data = (float*) memAlloc(sizeofDataType(FLOAT)*m_recvcount);
        m_data_validation_send = (float*) memAlloc(sizeofDataType(FLOAT)*m_recvcount);
        m_data_validation_recv = (float*) memAlloc(sizeofDataType(FLOAT)*m_recvcount);
        for(size_t i = 0; i < m_recvcount; i++){
            m_data[i] = rand() % 1024;
            m_data_validation_send[i] = m_data[i];
        }
        m_validation_reduce_executed = false;
    }
    m_mask = 1;
    assert(ceil(log2(size())) == floor(log2(size()))); // For now it only works on powers of 2
}

EmberTreeAllreduceGenerator::~EmberTreeAllreduceGenerator()
{
    ;
}

// Returns true when it's done
bool EmberTreeAllreduceGenerator::generate(std::queue<EmberEvent *> &evQ)
{

    if(m_mask < size()) {
        int partner = rank() ^ m_mask; // XOR with mask to get partner rank
        enQ_sendrecv(evQ,
					 m_data, m_recvcount, FLOAT, partner, 0,
					 m_tmp_data, m_recvcount, FLOAT, partner,  0,
					 GroupWorld, &m_resp );
        // Sum recv data to local
        if(m_validate){
            enQ_compute(evQ, [&]() {
                for(size_t i = 0; i < m_recvcount; i++){
                    m_data[i] += m_tmp_data[i];
                }
                return 0;
            });
        }
        m_mask <<= 1; // update the mask for the next step
        return false;
    }

    // Broadcast over over, we can return true
    // If we need to validate, we run a standard allreduce
    if(m_validate){
        if(!m_validation_reduce_executed){
            enQ_allreduce(evQ, m_data_validation_send, m_data_validation_recv, m_recvcount, FLOAT, Hermes::MP::SUM, GroupWorld);
            m_validation_reduce_executed = true;
            return false;
        }else{
            bool valid = true;
            for(size_t i = 0; i < m_recvcount; i++){
                if(m_data[i] != m_data_validation_recv[i]){
                    fprintf(stderr, "Validation error on rank %d at index %d (%f vs. %f)\n", rank(), i, m_data[i], m_data_validation_recv[i]);
                    valid = false;
                }
            }
            if(valid){
                DPRINTF("[Rank %d] Validation succeeded.\n", rank());
            }
        }
    }
    return true;
}