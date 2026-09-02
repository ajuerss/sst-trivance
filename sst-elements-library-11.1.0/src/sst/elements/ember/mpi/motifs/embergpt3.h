#ifndef _H_EMBER_GPT3_MOTIF
#define _H_EMBER_GPT3_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberGPT3Generator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberGPT3Generator,
        "ember",
        "GPT3Motif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Runs GPT3",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.all_reduce_type", "All Reduce Type (05D | Torus | Rings)", "05D"},
        {"arg.compscaling", "", "1"},
        {"arg.dimensions", "Topology dimensions", "1"},
        {"arg.dimensions_sizes", "Comma-separated list of size of each dimension.", ""},
    )

public:
	EmberGPT3Generator(SST::ComponentId_t, Params& params);
    ~EmberGPT3Generator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    bool setup_comms(std::queue<EmberEvent *> &evQ);
    bool progress_stage_fwd(std::queue<EmberEvent *> &evQ);
    bool progress_stage_bwd(std::queue<EmberEvent *> &evQ);
    bool progress_iteration(std::queue<EmberEvent *> &evQ);

private:
    enum gpt3_state_t {
        SETUP = 0,
        ITERATE,
    };

    enum iteration_state_t {
        INIT = 0,
        ALLREDUCE_1,
        ALLREDUCE_2,
        P2P,
        P2P_WAIT
    };

private:
    uint32_t m_dp_allreduce_group_rank, m_mp_pp_group_rank, m_mp_allreduce_group_rank, m_pp_p2p_group_rank;
    int m_dp_allreduce_group_size, m_mp_pp_group_size, m_mp_allreduce_group_size, m_pp_p2p_group_size;

    Communicator m_dp_allreduce_comm;
    Communicator m_mp_pp_comm;
    Communicator m_mp_allreduce_comm;
    Communicator m_pp_p2p_comm;

    uint64_t count_compute_time = 0;

    int m_setup_stage;
    uint32_t m_num_stage;

    double m_aggregation_cost_ns;
    gpt3_state_t m_state;
    iteration_state_t m_iteration_state;
    MessageRequest m_reqs[2];

    uint32_t m_grad_acc_step_idx, m_grad_acc_step;
    uint32_t m_acc_step_scale;
    uint32_t m_iter_idx;
    uint32_t px;

    uint32_t m_stage_id;
    std::string all_reduce_type;

    EmberRingAllreduce* m_mp_allreduce;
    EmberRingAllreduce* m_dp_allreduce;

    float* m_fwd_send_buff;
    float* m_fwd_recv_buff;
    float* m_bwd_send_buff;
    float* m_bwd_recv_buff;

    uint dimensions;    
    uint* dimensions_sizes = NULL;
    uint64_t startTime = 0, stopTime, pollingStart;
    bool startTimeTaken = false;
    uint compscaling;
};

}
}

#endif
