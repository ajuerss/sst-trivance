#ifndef _H_EMBER_GPT3_MOTIF
#define _H_EMBER_GPT3_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberGPT3MOEGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberGPT3MOEGenerator,
        "ember",
        "GPT3MOEMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Runs GPT3 MOE",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.all_reduce_type", "All Reduce Type (05D | 2D | 25D)", "05D"},
    )

public:
	EmberGPT3MOEGenerator(SST::ComponentId_t, Params& params);
    ~EmberGPT3MOEGenerator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    bool setup_comms(std::queue<EmberEvent *> &evQ);
    bool progress_stage_fwd(std::queue<EmberEvent *> &evQ);
    bool progress_stage_bwd(std::queue<EmberEvent *> &evQ);
    bool progress_iteration(std::queue<EmberEvent *> &evQ);

private:
    enum gpt3_state_t {
        SETUP,
        ITERATE,
    };

    enum iteration_state_t {
        INIT = 0,
        ALLTOALL_1,
        ALLTOALL_2,
        ISEND,
        WAIT_ALL
    };

private:
    uint32_t m_dp_group_rank, m_pp_p2p_group_rank;
    int m_dp_group_size, m_pp_p2p_group_size;
    uint32_t m_moe_allreduce_group_rank, m_moe_alltoall_group_rank;
    int m_moe_allreduce_group_size, m_moe_alltoall_group_size;

    Communicator m_dp_allreduce_comm;
    Communicator m_pp_p2p_comm;
    Communicator m_moe_alltoall_comm;
    Communicator m_moe_allreduce_comm;

    uint64_t count_compute_time = 0;

    int m_setup_stage;
    uint32_t m_num_stage, m_num_layer, m_num_moe;

    double m_aggregation_cost_ns;
    uint32_t m_alltoall_blocks;
    gpt3_state_t m_state;
    iteration_state_t m_iteration_state;
    MessageRequest m_reqs[2];

    uint32_t m_grad_acc_step_idx, m_grad_acc_step;
    uint32_t m_acc_step_scale;
    uint32_t m_iter_idx;
    uint32_t px;

    std::string all_reduce_type;

    uint32_t m_stage_id;

    EmberRingAllreduce* m_moe_grad_allreduce;
    EmberRingAllreduce* m_grad_allreduce;
    EmberImprovedAlltoall* m_alltoall;

    float* m_fwd_send_buff;
    float* m_fwd_recv_buff;
    float* m_bwd_send_buff;
    float* m_bwd_recv_buff;

    float* m_moe_fwd_alltoall_send_ptrs[2];
    float* m_moe_fwd_alltoall_recv_ptrs[2];
    float* m_moe_bwd_alltoall_send_ptrs[2];
    float* m_moe_bwd_alltoall_recv_ptrs[2];

};

}
}

#endif
