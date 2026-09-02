#ifndef _H_EMBER_COSMOFLOW_MOTIF
#define _H_EMBER_COSMOFLOW_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

#define NUM_L 8
#define NUM_Conv_L 5
#define NUM_Dense_L 3

namespace SST {
namespace Ember {

class EmberCosmoFlowGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberCosmoFlowGenerator,
        "ember",
        "CosmoFlowMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Runs CosmoFlow",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.all_reduce_type", "All Reduce Type (05D | 2D | 25D)", "05D"},
    )

public:
	EmberCosmoFlowGenerator(SST::ComponentId_t, Params& params);
    ~EmberCosmoFlowGenerator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    bool setup_comms(std::queue<EmberEvent *> &evQ);
    bool progress_stage_fwd(std::queue<EmberEvent *> &evQ);
    bool progress_stage_bwd(std::queue<EmberEvent *> &evQ);
    bool progress_iteration(std::queue<EmberEvent *> &evQ);
    EmberRingAllreduce* makeAllGather(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm);
    EmberRingAllreduce* makeReduceScatter(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm);
    EmberRingAllreduce* makeIAllreduce(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm);
    void temp_fix_all_reduce_size();

private:
    enum cosmoflow_state_t {
        SETUP = 0,
        ITERATE
    };

    enum iteration_state_t {
        IT_FORWARD = 0,
        IT_BACKWARD
    };

    enum fwd_state_t {
        FWD_INIT = 0,
        FWD_CONV,
        FWD_CONV_WAIT,
        FWD_ALLGATHER
    };

    enum bwd_state_t {
        BWD_REDUCE_SCATTER = 0,
        BWD_CONV,
        BWD_CONV_WAIT,
        BWD_IALLREDUCE
    };

private:

    int m_conv_fwd_halo_sizes[NUM_Conv_L-1] = {2097152, 1048576, 524288, 262144};
    int m_conv_bwd_halo_sizes[NUM_Conv_L-1] = {131072, 262144, 524288, 1048576};

    int m_dense_fwd_allgather_sizes[NUM_Dense_L] = {65536, 256, 128};
    int m_dense_bwd_reduce_scatter_sizes[NUM_Dense_L] = {128, 256, 65536};
    int m_allreduce_sizes[NUM_L-2] = {1050737, 3539456, 884992, 221312, 55360, 3488};
    //int m_allreduce_sizes[NUM_L-2] = {8192, 8192, 8192, 8192, 8192, 8192};

    uint64_t count_compute_time = 0;

    // in ns
    int m_fwd_rt_per_layer[NUM_L] = {6567*1000, 13135*1000, 6567*1000, 3283*1000, 1641*1000, 5*1000, 3*1000, 1*1000};
    int m_bwd_rt_per_layer[NUM_L] = {2*1000, 6*1000, 10*1000, 3283*1000, 6567*1000, 13135*1000, 26270*1000, 13135*1000};

    float* m_fwd_halo_send_buff0_ptrs[NUM_Conv_L-1];
    float* m_fwd_halo_send_buff1_ptrs[NUM_Conv_L-1];
    float* m_fwd_halo_recv_buff0_ptrs[NUM_Conv_L-1];
    float* m_fwd_halo_recv_buff1_ptrs[NUM_Conv_L-1];

    float* m_bwd_halo_send_buff0_ptrs[NUM_Conv_L-1];
    float* m_bwd_halo_send_buff1_ptrs[NUM_Conv_L-1];
    float* m_bwd_halo_recv_buff0_ptrs[NUM_Conv_L-1];
    float* m_bwd_halo_recv_buff1_ptrs[NUM_Conv_L-1];

    float* m_dense_fwd_allgather_sbuff_ptrs[NUM_Dense_L];
    float* m_dense_fwd_allgather_rbuff_ptrs[NUM_Dense_L];
    float* m_dense_bwd_rs_sbuff_ptrs[NUM_Dense_L];
    float* m_dense_bwd_rs_rbuff_ptrs[NUM_Dense_L];

    float* m_grad_ptrs[NUM_L-2];
    float* m_sum_grad_ptrs[NUM_L-2];

    int m_model_shards;

    int m_rank, m_size;
    uint32_t m_dense_allreduce_group_rank, m_mp_group_rank;
    int m_dense_allreduce_group_size, m_mp_group_size;

    int m_dense_allreduce_group_color;

    Communicator m_dense_allreduce_comm;
    Communicator m_model_parallel_comm;

    MessageRequest m_reqs[4];

    cosmoflow_state_t m_state;
    int m_setup_stage;
    iteration_state_t m_iteration_state;
    int m_fwd_i, m_bwd_i;
    fwd_state_t m_fwd_stage;
    bwd_state_t m_bwd_stage;
    int m_bwd_t_b;

    std::vector<EmberRingAllreduce*> m_bwd_allreduces_progress;
    std::vector<EmberRingAllreduce*> m_bwd_allreduces;
    std::vector<EmberRingAllreduce*> m_fwd_allgather;
    std::vector<EmberRingAllreduce*> m_bwd_reduce_scatter;

    int m_px;
    std::string all_reduce_type;
    double m_aggregation_cost_ns;
};

}
}

#endif
