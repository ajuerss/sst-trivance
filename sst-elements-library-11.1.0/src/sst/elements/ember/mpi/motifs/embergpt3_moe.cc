#include <sst_config.h>
#include "embergpt3_moe.h"

using namespace SST::Ember;

#define NUM_L 96
#define NUM_MOE 16
#define ACC_STEP_SCALE 2

// msg sizes for GPT-3 (M_dim=12288) with micro-batch size=1 and seq_len=2048
#define PIPE_P2P_SIZE       25165824
#define MP_ALLREDUCE_SIZE   25165824
#define MOE_ALL2ALL_SIZE    25165824
#define MHA_SIZE            603979776 // num params of mha in a layer
#define MLP_SIZE            1207959552 // num params of mlp in a layer

// runtime in us (10E-6)
#define FWD_MHA (22367*1000)
#define BWD_MHA (44734*1000)
#define FWD_MLP (41293*1000)
#define BWD_MLP (82586*1000)


/*#define PIPE_P2P_SIZE       4096
#define MP_ALLREDUCE_SIZE   4096
#define MOE_ALL2ALL_SIZE    4096
#define MHA_SIZE            4096 // num params of mha in a layer
#define MLP_SIZE            4096 // num params of mlp in a layer

// runtime in us (10E-6)
#define FWD_MHA 1
#define BWD_MHA 1
#define FWD_MLP 1
#define BWD_MLP 1*/

bool EmberGPT3MOEGenerator::progress_iteration(std::queue<EmberEvent *> &evQ)
{

    switch (m_iteration_state)
    {
    case INIT:
    {
        printf("[%d] init\n", rank());
        if(m_stage_id % 2 == 0){
            //MPI_Irecv(bwd_recv_buff, PIPE_P2P_SIZE, MPI_FLOAT, stage_id+1, 1, pp_p2p_comm, &reqs[0]); //receive input of next mb
            //usleep(FWD_MHA); //compute fwd
            //usleep(FWD_MLP/num_moe);
            enQ_irecv(evQ, m_bwd_recv_buff, PIPE_P2P_SIZE, FLOAT, m_stage_id+1, 1, m_pp_p2p_comm, &m_reqs[0]);
            enQ_compute(evQ, FWD_MHA);
            enQ_compute(evQ, FWD_MLP/m_num_moe);
            count_compute_time += FWD_MHA;
            count_compute_time += FWD_MLP/m_num_moe;
        } else {
            //MPI_Irecv(fwd_recv_buff, PIPE_P2P_SIZE, MPI_FLOAT, stage_id-1, 2, pp_p2p_comm, &reqs[1]); //receive input of next mb
            //usleep(BWD_MHA); //compute bwd
            //usleep(BWD_MLP/num_moe);
            enQ_irecv(evQ, m_fwd_recv_buff, PIPE_P2P_SIZE, FLOAT, m_stage_id-1, 2, m_pp_p2p_comm, &m_reqs[0]);
            enQ_compute(evQ, BWD_MHA);
            enQ_compute(evQ, BWD_MLP/m_num_moe);
            count_compute_time += BWD_MHA;
            count_compute_time += BWD_MLP/m_num_moe;
        }
        m_iteration_state = ALLTOALL_1;
        // no break
    }
    case ALLTOALL_1:
    {
        printf("[%d] alltoall1\n", rank());
        if (!m_alltoall->progress(evQ)) return false;
        m_alltoall->reset();
        m_iteration_state = ALLTOALL_2;  
        // no break
    }
    case ALLTOALL_2:
    {
        printf("[%d] alltoall2\n", rank());
        if (!m_alltoall->progress(evQ)) return false;
        m_iteration_state = ISEND;
        // no break
    }
    case ISEND:
    {
        printf("[%d] isend\n", rank());
        if (m_stage_id % 2 == 0) {
            //MPI_Isend(fwd_send_buff, PIPE_P2P_SIZE, MPI_FLOAT, stage_id+1, 2, pp_p2p_comm, &reqs[1]); //send output of current mb
            enQ_isend(evQ, m_fwd_send_buff, PIPE_P2P_SIZE, FLOAT, m_stage_id+1, 2, m_pp_p2p_comm, &m_reqs[1]);
        } else {
            //MPI_Isend(bwd_send_buff, PIPE_P2P_SIZE, MPI_FLOAT, stage_id-1, 1, pp_p2p_comm, &reqs[0]); //send output of current mb
            enQ_isend(evQ, m_bwd_send_buff, PIPE_P2P_SIZE, FLOAT, m_stage_id-1, 1, m_pp_p2p_comm, &m_reqs[1]);
        }
        m_iteration_state = WAIT_ALL;
        return false;
    }
    case WAIT_ALL:
    {
        //MPI_Waitall(2, reqs, MPI_STATUS_IGNORE);
        enQ_waitall(evQ, 2, &m_reqs[0]);
        return true;
    }
    default: assert(0);
    }
    assert(0);
}

bool EmberGPT3MOEGenerator::setup_comms(std::queue<EmberEvent *> &evQ)
{
    // void rank( Queue& q, Communicator comm, uint32_t* rankPtr)
    // void size( Queue& q, Communicator comm, int* sizePtr)

    int world_size = size();
    int my_rank = rank();
    printf("world_size %d - Num Stage %d - m_num_moe %d\n", world_size, m_num_stage, m_num_moe); fflush(stdout);
    assert(world_size % (m_num_stage * m_num_moe) == 0);

    switch (m_setup_stage)
    {
    case 0:
    {
        int dp_group_color = my_rank % m_num_stage;
        printf("[%d] STAGE 0: m_dp_allreduce_comm (color: %d; key: %d)\n", rank(), dp_group_color, my_rank);
        enQ_commSplit(evQ, GroupWorld, dp_group_color, my_rank, &m_dp_allreduce_comm);
        m_setup_stage++;
        return false;
    }
    case 1:
    {
        enQ_rank(evQ, m_dp_allreduce_comm, &m_dp_group_rank);
        enQ_size(evQ, m_dp_allreduce_comm, &m_dp_group_size);
        printf("[%d] STAGE 1\n", rank());

        m_setup_stage++;
        return false;
    }
    case 2:
    {
        printf("[%d] STAGE 2: m_dp_allreduce_comm: m_dp_group_rank: %d; m_dp_group_size: %d\n", rank(), m_dp_group_rank, m_dp_group_size);
        printf("[%d] STAGE 2: SPLIT m_mp_pp_comm (color: %d; key: %d)\n", rank(), m_dp_group_rank, my_rank);
        enQ_commSplit(evQ, GroupWorld, m_dp_group_rank, my_rank, &m_pp_p2p_comm);

        m_setup_stage++;
        return false;
    }
    case 3:
    {
        printf("[%d] STAGE 3\n", rank());
        enQ_rank(evQ, m_pp_p2p_comm, &m_pp_p2p_group_rank);
        enQ_size(evQ, m_pp_p2p_comm, &m_pp_p2p_group_size);

        m_setup_stage++;
        return false;
    }
    case 4:
    {
        int moe_allreduce_group_color = m_dp_group_rank % m_num_moe;

        printf("[%d] STAGE 4: m_pp_p2p_comm: m_pp_p2p_group_rank: %d; m_pp_p2p_group_size: %d\n", rank(), m_pp_p2p_group_rank, m_pp_p2p_group_size);
        printf("[%d] STAGE 4: SPLIT m_moe_allreduce_comm (color: %d; key: %d)\n", rank(), moe_allreduce_group_color, m_dp_group_rank);

        enQ_commSplit(evQ, m_dp_allreduce_comm, moe_allreduce_group_color, m_dp_group_rank, &m_moe_allreduce_comm);

        m_setup_stage++;
        return false;
    }
    case 5:
    {
        printf("[%d] STAGE 5\n", rank());
        enQ_rank(evQ, m_moe_allreduce_comm, &m_moe_allreduce_group_rank);
        enQ_size(evQ, m_moe_allreduce_comm, &m_moe_allreduce_group_size);

        m_setup_stage++;
        return false;
    }
    case 6:
    {
        printf("[%d] STAGE 6: m_moe_allreduce_comm: m_moe_allreduce_group_rank: %d; m_moe_allreduce_group_size: %d\n", rank(), m_moe_allreduce_group_rank, m_moe_allreduce_group_size);
        printf("[%d] STAGE 6: SPLIT m_moe_alltoall_comm (color: %d; key: %d)\n", rank(), m_moe_allreduce_group_rank, m_dp_group_rank);

        enQ_commSplit(evQ, m_dp_allreduce_comm, m_moe_allreduce_group_rank, m_dp_group_rank, &m_moe_alltoall_comm);

        m_setup_stage++;
        return false;
    }
    case 7:
    {
        printf("[%d] STAGE 7\n", rank());
        // MPI_Comm_rank(pp_p2p_comm, &pp_p2p_group_rank);
        enQ_rank(evQ, m_moe_alltoall_comm, &m_moe_alltoall_group_rank);
        // MPI_Comm_size(pp_p2p_comm, &pp_p2p_group_size);
        enQ_size(evQ, m_moe_alltoall_comm, &m_moe_alltoall_group_size);

        m_setup_stage++;
        return false;
    }
    case 8:
    {
        assert(m_pp_p2p_group_size == m_num_stage);
        assert(m_moe_alltoall_group_size == m_num_moe);
        assert(m_dp_group_size == m_num_moe * m_moe_allreduce_group_size);
        printf("[%d] STAGE 8: m_moe_alltoall_comm: m_moe_alltoall_group_rank: %d; m_moe_alltoall_group_size: %d\n", rank(), m_moe_alltoall_group_rank, m_moe_alltoall_group_size);

        //m_moe_grad_allreduce = new EmberRingAllreduce2D(*this, MLP_SIZE/m_num_moe, m_moe_allreduce_group_rank, m_moe_allreduce_group_size, m_moe_allreduce_comm, m_aggregation_cost_ns, false);
        //m_grad_allreduce = new EmberRingAllreduce2D(*this, MHA_SIZE, m_dp_group_rank, m_dp_group_size, m_dp_allreduce_comm, m_aggregation_cost_ns, false);

        if (!all_reduce_type.compare("05D")) {
            m_moe_grad_allreduce = new EmberRingAllreduce05D(*this, MLP_SIZE/m_num_moe, m_moe_allreduce_group_rank, m_moe_allreduce_group_size, m_moe_allreduce_comm, m_aggregation_cost_ns, false);
            m_grad_allreduce = new EmberRingAllreduce05D(*this, MHA_SIZE, m_dp_group_rank, m_dp_group_size, m_dp_allreduce_comm, m_aggregation_cost_ns, false);
        } else if (!all_reduce_type.compare("2D")) {
            m_moe_grad_allreduce = new EmberRingAllreduce2D(*this, MLP_SIZE/m_num_moe, m_moe_allreduce_group_rank, m_moe_allreduce_group_size, m_moe_allreduce_comm, m_aggregation_cost_ns, false, px);
            m_grad_allreduce = new EmberRingAllreduce2D(*this, MHA_SIZE, m_dp_group_rank, m_dp_group_size, m_dp_allreduce_comm, m_aggregation_cost_ns, false, px);
        } else if (!all_reduce_type.compare("25D")) {
            m_moe_grad_allreduce = new EmberRingAllreduce25D(*this, MLP_SIZE/m_num_moe, m_moe_allreduce_group_rank, m_moe_allreduce_group_size, m_moe_allreduce_comm, m_aggregation_cost_ns, false, px);
            m_grad_allreduce = new EmberRingAllreduce25D(*this, MHA_SIZE, m_dp_group_rank, m_dp_group_size, m_dp_allreduce_comm, m_aggregation_cost_ns, false, px);
        } else {
            printf("Unexpected branch with all reduce type! Exiting!\n");
            exit(EXIT_FAILURE);
        }
        printf("Using %s All Reduce\n", all_reduce_type.c_str());

        //m_alltoall = new EmberRandAlltoall(*this, MOE_ALL2ALL_SIZE/m_num_moe, m_alltoall_blocks, m_moe_alltoall_group_rank, m_moe_alltoall_group_size, m_moe_alltoall_comm, false, true);
        m_alltoall = new EmberImprovedAlltoall(*this, MOE_ALL2ALL_SIZE/m_num_moe, m_moe_alltoall_group_rank, m_moe_alltoall_group_size, m_moe_alltoall_comm);

        m_stage_id = m_pp_p2p_group_rank;

        enQ_barrier(evQ, GroupWorld);

        m_setup_stage++; // we increase it anyway so we go to assert(0) if this gets erronously called again
        return true;
    }
    default: assert(0);
    }
    assert(0);
}

bool EmberGPT3MOEGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    switch (m_state) {
    case SETUP:
        if (!setup_comms(evQ)) return false;
        m_state = ITERATE;
        // no break
    case ITERATE:
        if (!progress_iteration(evQ)) return false;
        return true;
    default: assert(0);
    }
    assert(0);
}

EmberGPT3MOEGenerator::EmberGPT3MOEGenerator(SST::ComponentId_t id, Params &params)
    : EmberHxMeshGenerator(id, params, "GPT3MOE"), 
      m_state(SETUP),  
      m_setup_stage(0), 
      m_iteration_state(INIT)
{
    m_aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);

    m_num_stage = (uint32_t)params.find("arg.num_stage", NUM_L);
    m_num_layer = (uint32_t)params.find("arg.num_layer", NUM_L);
    m_acc_step_scale = (uint32_t)params.find("arg.acc_step_stale", ACC_STEP_SCALE);
    m_grad_acc_step = (uint32_t)params.find("arg.grad_acc_step", m_num_stage * m_acc_step_scale);
    m_alltoall_blocks = (uint32_t)params.find("arg.alltoall_blocks", 1);
    px = (uint32_t)params.find("arg.px", 0);
   all_reduce_type = params.find<std::string>("arg.all_reduce_type", "05D");
    if (all_reduce_type.compare("05D") && all_reduce_type.compare("2D") && all_reduce_type.compare("25D")) {
        printf("Erorr, non valid all reduce type!\n"); 
        exit(EXIT_FAILURE);
    }
   
    m_num_moe = NUM_MOE;

    m_fwd_send_buff = (float *) memAlloc(PIPE_P2P_SIZE*sizeof(float));
    m_fwd_recv_buff = (float *) memAlloc(PIPE_P2P_SIZE*sizeof(float));
    m_bwd_send_buff = (float *) memAlloc(PIPE_P2P_SIZE*sizeof(float));
    m_bwd_recv_buff = (float *) memAlloc(PIPE_P2P_SIZE*sizeof(float));

    for(int i=0; i<2; i++){
        m_moe_fwd_alltoall_send_ptrs[i] = (float *) memAlloc(MOE_ALL2ALL_SIZE*sizeof(float));
        m_moe_fwd_alltoall_recv_ptrs[i] = (float *) memAlloc(MOE_ALL2ALL_SIZE*sizeof(float));
        m_moe_bwd_alltoall_send_ptrs[i] = (float *) memAlloc(MOE_ALL2ALL_SIZE*sizeof(float));
        m_moe_bwd_alltoall_recv_ptrs[i] = (float *) memAlloc(MOE_ALL2ALL_SIZE*sizeof(float));
    }
}

EmberGPT3MOEGenerator::~EmberGPT3MOEGenerator()
{
    printf("\nRank %d - Total Compute time %" PRIu64 " \n", rank(), count_compute_time);
    delete m_moe_grad_allreduce;
    delete m_grad_allreduce;

    memFree(m_fwd_send_buff);
    memFree(m_fwd_recv_buff);
    memFree(m_bwd_send_buff);
    memFree(m_bwd_recv_buff);

    for(int i=0; i<2; i++){
        memFree(m_moe_fwd_alltoall_send_ptrs[i]);
        memFree(m_moe_fwd_alltoall_recv_ptrs[i]);
        memFree(m_moe_bwd_alltoall_send_ptrs[i]);
        memFree(m_moe_bwd_alltoall_recv_ptrs[i]);
    }
}