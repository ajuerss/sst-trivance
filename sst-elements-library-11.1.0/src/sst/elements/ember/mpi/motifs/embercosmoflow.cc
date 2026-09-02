#include <sst_config.h>
#include "embercosmoflow.h"
#include "emberhxmesh.h"

#define COMPUTE_STEP 100

using namespace SST::Ember;

bool EmberCosmoFlowGenerator::progress_iteration(std::queue<EmberEvent *> &evQ)
{
    switch (m_iteration_state)
    {
    case IT_FORWARD:
        if (!progress_stage_fwd(evQ)) return false;
        m_iteration_state = IT_BACKWARD;
        //no break
    case IT_BACKWARD:
        if (!progress_stage_bwd(evQ)) return false;
        m_iteration_state = IT_FORWARD;
        m_fwd_i = 0;
        m_bwd_i = 0;
        return true;
    default: assert(0);
    }
    assert(0);
}

bool EmberCosmoFlowGenerator::progress_stage_fwd(std::queue<EmberEvent *> &evQ)
{
    assert(m_fwd_i>=0 && m_fwd_i < NUM_L);
    
    switch (m_fwd_stage)
    {
    case FWD_INIT:
    {
        printf("[%d] m_fwd_i: %d; compute\n", m_rank, m_fwd_i);
        enQ_compute(evQ, m_fwd_rt_per_layer[m_fwd_i]); //compute
        count_compute_time += m_fwd_rt_per_layer[m_fwd_i];
        m_fwd_i++;
        m_fwd_stage = FWD_CONV;
        return false;
    }
    case FWD_CONV:
    {
        assert(m_fwd_i >= 1 && m_fwd_i < NUM_Conv_L);
        printf("[%d] m_fwd_i: %d; conv\n", m_rank, m_fwd_i);
        //halo exchange for conv layers
        int msg_idx = m_fwd_i-1;
   
        enQ_isend(evQ, m_fwd_halo_send_buff0_ptrs[msg_idx], m_conv_fwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^1, m_fwd_i, m_model_parallel_comm, &m_reqs[0]);
        enQ_isend(evQ, m_fwd_halo_send_buff1_ptrs[msg_idx], m_conv_fwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^2, m_fwd_i, m_model_parallel_comm, &m_reqs[1]);
        enQ_irecv(evQ, m_fwd_halo_recv_buff0_ptrs[msg_idx], m_conv_fwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^1, m_fwd_i, m_model_parallel_comm, &m_reqs[2]);
        enQ_irecv(evQ, m_fwd_halo_recv_buff1_ptrs[msg_idx], m_conv_fwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^2, m_fwd_i, m_model_parallel_comm, &m_reqs[3]);
        m_fwd_stage = FWD_CONV_WAIT;
        return false;
    }
    case FWD_CONV_WAIT:
    {
        //wait
        enQ_waitall(evQ, 4, &m_reqs[0]);

        m_fwd_i++;
        if (m_fwd_i < NUM_Conv_L) m_fwd_stage = FWD_CONV;
        else m_fwd_stage = FWD_ALLGATHER;
        printf("[%d] m_fwd_i: %d; conv_wait is over -> compute\n", m_rank, m_fwd_i);
        enQ_compute(evQ, m_fwd_rt_per_layer[m_fwd_i]); //compute
        count_compute_time += m_fwd_rt_per_layer[m_fwd_i];

        return false;
    }
    case FWD_ALLGATHER:
    {
        int msg_idx = m_fwd_i-NUM_Conv_L;
        if (!m_fwd_allgather[msg_idx]->progress(evQ)) return false;
        printf("[%d] m_fwd_i: %d; allgather completed -> compute\n", m_rank, m_fwd_i);
        m_fwd_allgather[msg_idx]->reset();
        m_fwd_i++;
        enQ_compute(evQ, m_fwd_rt_per_layer[m_fwd_i]); //compute
        count_compute_time += m_fwd_rt_per_layer[m_fwd_i];
        if (m_fwd_i == NUM_L) return true;
        else return false;
    }
    default: assert(0);
    }
}

bool EmberCosmoFlowGenerator::progress_stage_bwd(std::queue<EmberEvent *> &evQ)
{
    if (m_bwd_i == NUM_L)
    {
        // just wait for ongoing allreduces
        //printf("[%d] m_bwd_i: %d; loop over -> waiting for allreduces to complete\n", m_rank, m_bwd_i);
        bool all_completed = true;
        for (auto &allreduce : m_bwd_allreduces_progress)
            all_completed = all_completed && allreduce->progress(evQ);
        return all_completed;
    }

    assert(m_bwd_i >= 0 && m_bwd_i < NUM_L);

    // progress allreduces and compute something    
    bool all_completed = true;
    assert(m_bwd_allreduces_progress.empty() || m_bwd_i >= NUM_Dense_L - 1);
    for (auto &allreduce : m_bwd_allreduces_progress)
        all_completed = all_completed && allreduce->progress(evQ);

    if (all_completed) {
        assert(m_bwd_t_b <= m_bwd_rt_per_layer[m_bwd_i]);
        enQ_compute(evQ, m_bwd_rt_per_layer[m_bwd_i] - m_bwd_t_b);
        count_compute_time += m_bwd_rt_per_layer[m_bwd_i] - m_bwd_t_b;
        m_bwd_t_b = m_bwd_rt_per_layer[m_bwd_i]; 
    } 
    else
    {
        enQ_compute(evQ, COMPUTE_STEP); //compute
        count_compute_time += COMPUTE_STEP;
        m_bwd_t_b += COMPUTE_STEP;
    }

    if (m_bwd_t_b >= m_bwd_rt_per_layer[m_bwd_i])
    {
        // compute is over, do comm step
        switch (m_bwd_stage)
        {
        case BWD_REDUCE_SCATTER:
        {
            assert(m_bwd_i < NUM_Dense_L);
            if (!m_bwd_reduce_scatter[m_bwd_i]->progress(evQ)) return false;
            printf("[%d] m_bwd_i: %d; reduce scatter complete\n", m_rank, m_bwd_i);

            m_bwd_i++;

            if (m_bwd_i == NUM_Dense_L-1) 
            {
                printf("[%d] m_bwd_i: %d; m_bwd_i == NUM_Dense_L-1; first allreduce\n", m_rank, m_bwd_i);
                m_bwd_allreduces_progress.push_back(m_bwd_allreduces[0]);
            }

            if (m_bwd_i == NUM_Dense_L) 
            {
                printf("[%d] m_bwd_i: %d; m_bwd_i == NUM_Dense_L; moving to BWD_CONV\n", m_rank, m_bwd_i);
                m_bwd_stage = BWD_CONV;
            }
            return false;
        }
        case BWD_CONV:
        {
            assert(m_bwd_i < NUM_L-1);
            int msg_idx = m_bwd_i-NUM_Dense_L;
            printf("[%d] m_bwd_i: %d; BWD_CONV (msg_idx: %d)\n", m_rank, m_bwd_i, msg_idx);
            enQ_isend(evQ, m_bwd_halo_send_buff0_ptrs[msg_idx], m_conv_bwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^1, m_bwd_i, m_model_parallel_comm, &m_reqs[0]);
            enQ_isend(evQ, m_bwd_halo_send_buff1_ptrs[msg_idx], m_conv_bwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^2, m_bwd_i, m_model_parallel_comm, &m_reqs[1]);
            enQ_irecv(evQ, m_bwd_halo_recv_buff0_ptrs[msg_idx], m_conv_bwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^1, m_bwd_i, m_model_parallel_comm, &m_reqs[2]);
            enQ_irecv(evQ, m_bwd_halo_recv_buff1_ptrs[msg_idx], m_conv_bwd_halo_sizes[msg_idx], FLOAT, m_mp_group_rank^2, m_bwd_i, m_model_parallel_comm, &m_reqs[3]);
        
            m_bwd_stage = BWD_CONV_WAIT;
            return false;
        }
        case BWD_CONV_WAIT:
        {
            assert(m_bwd_i > NUM_Dense_L - 1);
            enQ_waitall(evQ, 4, &m_reqs[0]);
            printf("[%d] m_bwd_i: %d; BWD_CONV_WAIT is over\n", m_rank, m_bwd_i);
            // no break
        }
        case BWD_IALLREDUCE:
        {
            printf("[%d] m_bwd_i: %d; starting new allreduce (idx: %d)\n", m_rank, m_bwd_i, m_bwd_i-NUM_Dense_L+1);
            m_bwd_allreduces_progress.push_back(m_bwd_allreduces[m_bwd_i-NUM_Dense_L+1]);
            m_bwd_i++;
            if (m_bwd_i == NUM_L-1) m_bwd_stage = BWD_IALLREDUCE;
            else m_bwd_stage = BWD_CONV; 
            return false;
        }
        default: assert(0);
        }

        m_bwd_t_b = 0;
    } else return false;

    assert(0);
}

bool EmberCosmoFlowGenerator::setup_comms(std::queue<EmberEvent *> &evQ)
{
    switch(m_setup_stage)
    {
    case 0:
    {
        printf("[%d] Stage 0\n", m_rank);
        assert(m_size % m_model_shards == 0);
        m_dense_allreduce_group_color = m_rank % m_model_shards;
        enQ_commSplit(evQ, GroupWorld, m_dense_allreduce_group_color, m_rank, &m_dense_allreduce_comm);
        m_setup_stage++;
        return false;
    }
    case 1:
    {
        printf("[%d] Stage 1\n", m_rank);
        enQ_rank(evQ, m_dense_allreduce_comm, &m_dense_allreduce_group_rank);
        enQ_size(evQ, m_dense_allreduce_comm, &m_dense_allreduce_group_size);
        m_setup_stage++;
        return false;
    }
    case 2:
    {
        printf("[%d] Stage 2\n", m_rank);
        enQ_commSplit(evQ, GroupWorld, m_dense_allreduce_group_rank, m_rank, &m_model_parallel_comm);
        m_setup_stage++;
        return false;
    }
    case 3:
    {
        printf("[%d] Stage 3\n", m_rank);
        enQ_rank(evQ, m_model_parallel_comm, &m_mp_group_rank);
        enQ_size(evQ, m_model_parallel_comm, &m_mp_group_size);
        m_setup_stage++;
        return false;
    }
    case 4:
    {
        printf("[%d] Stage 4\n", m_rank);
        assert(m_dense_allreduce_group_color == m_mp_group_rank);
        assert(m_model_shards == m_mp_group_size);

        for (int i=0; i<NUM_Dense_L; i++)
        {
            m_fwd_allgather.push_back(makeAllGather(m_dense_fwd_allgather_sizes[i], m_mp_group_rank, m_mp_group_size, m_model_parallel_comm));
            m_bwd_reduce_scatter.push_back(makeReduceScatter(m_dense_bwd_reduce_scatter_sizes[i], m_mp_group_rank, m_mp_group_size, m_model_parallel_comm));
        }

        for (int i=0; i<NUM_L-2; i++)
        {
            m_bwd_allreduces.push_back(makeIAllreduce(m_allreduce_sizes[i], m_dense_allreduce_group_rank, m_dense_allreduce_group_size, m_dense_allreduce_comm));
        }

        enQ_barrier(evQ, GroupWorld);
        return true;
    }
    default: assert(0);
    }
    assert(0);
}

EmberHxMeshGenerator::EmberRingAllreduce* EmberCosmoFlowGenerator::makeAllGather(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm)
{    
    printf("All Reduce size1 %d\n", count); 
    EmberRingAllreduce* res = new EmberRingAllreduce05D(*this, count, rank, comm_size, comm, 0, false, /*m_px,*/ RING_ALLGATHER);
    assert(res!=NULL);
    return res;
}

EmberHxMeshGenerator::EmberRingAllreduce* EmberCosmoFlowGenerator::makeReduceScatter(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm)
{    
    printf("All Reduce size2 %d\n", count); 
    EmberRingAllreduce* res = new EmberRingAllreduce05D(*this, count, rank, comm_size, comm, m_aggregation_cost_ns, false, /*m_px,*/ RING_REDUCE_SCATTER);
    assert(res!=NULL);
    return res;
}


void EmberCosmoFlowGenerator::temp_fix_all_reduce_size() {

    for (int i = 0; i < 6; i++) {
        int current = m_allreduce_sizes[i]; 
        int min_div = size();
        int res = current / min_div;
        m_allreduce_sizes[i] = res * min_div;
    }
    return;
}

EmberHxMeshGenerator::EmberRingAllreduce* EmberCosmoFlowGenerator::makeIAllreduce(uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm)
{    
    //EmberRingAllreduce* res = new EmberRingAllreduce05D(*this, count, rank, comm_size, comm, m_aggregation_cost_ns, true, /*m_px,*/ RING_ALLREDUCE);
    EmberRingAllreduce* res = NULL;
    printf("All Reduce size3 %d - comm size %d\n", count, comm_size); 

    if (!all_reduce_type.compare("05D")) {
        res = new EmberRingAllreduce05D(*this, count, rank, comm_size, comm, m_aggregation_cost_ns, true, /*m_px,*/ RING_ALLREDUCE);
    } else if (!all_reduce_type.compare("2D")) {
        printf("Px is %d - Comm Size is %d - \n", m_px, comm_size);
        res = new EmberRingAllreduce2D(*this, count, rank, comm_size, comm, m_aggregation_cost_ns, true, m_px, RING_ALLREDUCE);
    } else if (!all_reduce_type.compare("25D")) {
        printf("Px is %d - Comm Size is %d - \n", m_px, comm_size);
        res = new EmberRingAllreduce25D(*this, count, rank, comm_size, comm, m_aggregation_cost_ns, true, m_px, RING_ALLREDUCE);
    } else {
        printf("Unexpected branch with all reduce type! Exiting!\n");
        exit(EXIT_FAILURE);
    }
    printf("Using %s All Reduce\n", all_reduce_type.c_str());
    
    assert(res!=NULL);
    return res;
}

bool EmberCosmoFlowGenerator::generate(std::queue<EmberEvent *> &evQ)
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

EmberCosmoFlowGenerator::EmberCosmoFlowGenerator(SST::ComponentId_t id, Params &params)
    : EmberHxMeshGenerator(id, params, "CosmoFlow"), 
      m_state(SETUP), 
      m_setup_stage(0), 
      m_iteration_state(IT_FORWARD),
      m_fwd_i(0),
      m_bwd_i(0),
      m_fwd_stage(FWD_INIT),
      m_bwd_stage(BWD_REDUCE_SCATTER),
      m_bwd_t_b(0),
      m_model_shards(4)
{

    m_aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    m_px = (uint32_t)params.find("arg.px", 0);

    all_reduce_type = params.find<std::string>("arg.all_reduce_type", "05D");
    if (all_reduce_type.compare("05D") && all_reduce_type.compare("2D") && all_reduce_type.compare("25D")) {
        printf("Error, non valid all reduce type!\n"); 
        exit(EXIT_FAILURE);
    }

    m_rank = rank();
    m_size = size();

    for(int i=0; i<NUM_Conv_L-1; i++){
        m_fwd_halo_send_buff0_ptrs[i] = (float *) memAlloc(m_conv_fwd_halo_sizes[i]*sizeof(float));
        m_fwd_halo_send_buff1_ptrs[i] = (float *) memAlloc(m_conv_fwd_halo_sizes[i]*sizeof(float));
        m_fwd_halo_recv_buff0_ptrs[i] = (float *) memAlloc(m_conv_fwd_halo_sizes[i]*sizeof(float));
        m_fwd_halo_recv_buff1_ptrs[i] = (float *) memAlloc(m_conv_fwd_halo_sizes[i]*sizeof(float));

        m_bwd_halo_send_buff0_ptrs[i] = (float *) memAlloc(m_conv_bwd_halo_sizes[i]*sizeof(float));
        m_bwd_halo_send_buff1_ptrs[i] = (float *) memAlloc(m_conv_bwd_halo_sizes[i]*sizeof(float));
        m_bwd_halo_recv_buff0_ptrs[i] = (float *) memAlloc(m_conv_bwd_halo_sizes[i]*sizeof(float));
        m_bwd_halo_recv_buff1_ptrs[i] = (float *) memAlloc(m_conv_bwd_halo_sizes[i]*sizeof(float));
    }

    for(int i=0; i<NUM_Dense_L; i++){
        m_dense_fwd_allgather_sbuff_ptrs[i] = (float *) memAlloc(m_dense_fwd_allgather_sizes[i]*sizeof(float));
        m_dense_fwd_allgather_rbuff_ptrs[i] = (float *) memAlloc(m_dense_fwd_allgather_sizes[i]*m_model_shards*sizeof(float));
        m_dense_bwd_rs_sbuff_ptrs[i] =        (float *) memAlloc(m_dense_bwd_reduce_scatter_sizes[i]*m_model_shards*sizeof(float));
        m_dense_bwd_rs_rbuff_ptrs[i] =        (float *) memAlloc(m_dense_bwd_reduce_scatter_sizes[i]*sizeof(float));
    }

    temp_fix_all_reduce_size();

    for(int i=0; i<NUM_L-2; i++){
        m_grad_ptrs[i]      = (float *) memAlloc(m_allreduce_sizes[i]*sizeof(float));
        m_sum_grad_ptrs[i]  = (float *) memAlloc(m_allreduce_sizes[i]*sizeof(float));
    }
}

EmberCosmoFlowGenerator::~EmberCosmoFlowGenerator()
{
    printf("\nRank %d - Total Compute time %" PRIu64 " \n", rank(), count_compute_time);
    for(int i=0; i<NUM_Conv_L-1; i++){
        memFree(m_fwd_halo_send_buff0_ptrs[i]);
        memFree(m_fwd_halo_send_buff1_ptrs[i]);
        memFree(m_fwd_halo_recv_buff0_ptrs[i]);
        memFree(m_fwd_halo_recv_buff1_ptrs[i]);

        memFree(m_bwd_halo_send_buff0_ptrs[i]);
        memFree(m_bwd_halo_send_buff1_ptrs[i]);
        memFree(m_bwd_halo_recv_buff0_ptrs[i]);
        memFree(m_bwd_halo_recv_buff1_ptrs[i]);
    }

    for(int i=0; i<NUM_Dense_L; i++){
        memFree(m_dense_fwd_allgather_sbuff_ptrs[i]);
        memFree(m_dense_fwd_allgather_rbuff_ptrs[i]);
        memFree(m_dense_bwd_rs_sbuff_ptrs[i]);
        memFree(m_dense_bwd_rs_rbuff_ptrs[i]);
    }

    for(int i=0; i<NUM_L-2; i++){
        memFree(m_grad_ptrs[i]);
        memFree(m_sum_grad_ptrs[i]);
    }

    m_bwd_allreduces.clear();
    m_bwd_reduce_scatter.clear();
    m_fwd_allgather.clear();   
}