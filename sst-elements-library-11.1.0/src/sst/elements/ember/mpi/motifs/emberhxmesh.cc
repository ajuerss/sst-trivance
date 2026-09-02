#include <sst_config.h>
#include <sst/core/rng/xorshift.h>
#include <limits.h>
#include "emberhxmesh.h"
#include <cmath>

//#define VALIDATE 
//#define DEBUG
#define MAX_SUPPORTED_DIMENSIONS 8

using namespace SST::Ember;
using namespace SST::RNG;

extern "C" 
{
#include "hxmesh_dims.h"
}

#ifdef DEBUG
#define DPRINTF(...) printf(__VA_ARGS__)
#else
#define DPRINTF(...) 
#endif

#define TAG 0xDEADBEEF


EmberHxMeshGenerator::RingAllreduce::RingAllreduce(EmberHxMeshGenerator &gen, RingType ring_type, float *dst, uint32_t count, uint32_t vrank, uint32_t numproc, uint32_t recvfrom, uint32_t sendto, 
                                                   double aggregation_cost_ns, Communicator comm, bool validate)
    :  m_gen(gen), m_count(count), m_dst(dst), m_r(vrank), m_p(numproc), m_recvfrom(recvfrom), m_sendto(sendto), m_aggregation_cost_ns(aggregation_cost_ns), m_data_sent(0), m_comm(comm), m_enabled(true), m_validate(validate)
{
    uint32_t block_size = (m_count >= m_p) ? m_count / m_p : m_count;
    printf("Count1 is %d - m_p is %d - block size %d\n",count, m_p, block_size); fflush(stdout);

    if(m_validate){
        m_tmp = (float*) malloc(gen.sizeofDataType(FLOAT)*block_size);
        assert(m_tmp!=NULL);
    }else{
        m_tmp = NULL;
    }

   switch (ring_type) {
    case RING_ALLREDUCE:
        m_do_reduce_scatter = true;
        m_do_allgather = true;
        break;
    case RING_REDUCE_SCATTER:
        m_do_reduce_scatter = true;
        m_do_allgather = false;
        break;
    case RING_ALLGATHER:
        m_do_reduce_scatter = false;
        m_do_allgather = true;
        break;
    default: assert(0);
    }

    reset();
    printf("Count2 is %d - m_p is %d - block size %d\n",count, m_p, block_size); fflush(stdout);
    //assert(count >= m_p);
    
    //assert((count / m_p) * m_p == count);
}

void EmberHxMeshGenerator::RingAllreduce::setEnable(bool enable)
{
    m_enabled = enable;
}

bool EmberHxMeshGenerator::RingAllreduce::isEnabled()
{
    return m_enabled;
}

bool EmberHxMeshGenerator::RingAllreduce::progress(std::queue<EmberEvent *> &evQ) 
{
    switch (m_state)
    {
    case REDUCE_SCATTER: 
        if (!m_do_reduce_scatter || reduceScatter(evQ)) 
        {
            m_state = ALL_GATHER;
        } else return false;
    case ALL_GATHER:
        if (!m_do_allgather || allGather(evQ))
        {
            m_state = FINI;
        } else return false;
    case FINI:
        return true;
    default: assert(0);
    }
    assert(0);
}

uint64_t EmberHxMeshGenerator::RingAllreduce::getMovedBytes()
{
    return m_data_sent;
}

void EmberHxMeshGenerator::RingAllreduce::setBuff(float *new_dest)
{
    m_dst = new_dest;
}

float* EmberHxMeshGenerator::RingAllreduce::getBuff()
{
    return m_dst;
}

bool EmberHxMeshGenerator::RingAllreduce::hasPendingRecv()
{
    return m_waiting_recv && m_ready_to_recv;
}

MessageRequest EmberHxMeshGenerator::RingAllreduce::getRecvHandle()
{
    return m_req_recv;
}

void EmberHxMeshGenerator::RingAllreduce::reset()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
    m_i = 0;
    m_state = REDUCE_SCATTER;
    m_tag1 = m_gen.getNextTag();
    m_tag2 = m_gen.getNextTag();
}

void EmberHxMeshGenerator::RingAllreduce::notifyRecv()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
}

void EmberHxMeshGenerator::RingAllreduce::aggregate(std::queue<EmberEvent *> &evQ, float* a, float* b, uint32_t num)
{

    if(m_validate){
        for (int i=0; i<num; i++) a[i] += b[i];
    }else{
        m_gen.enQ_compute(evQ, m_aggregation_cost_ns * num);
    }
}

uint32_t EmberHxMeshGenerator::RingAllreduce::getBlockOffset(uint32_t block_idx)
{
    if (m_count < m_p) return 0;

    uint32_t block_size = m_count / m_p;
    return block_idx * block_size;
}

uint32_t EmberHxMeshGenerator::RingAllreduce::getBlockSize(uint32_t block_idx)
{
    if (m_count < m_p) return m_count;

    uint32_t block_size = m_count / m_p;
    uint32_t num_blocks = m_p;
    assert(block_idx >= 0 && block_idx < num_blocks);
    if (block_idx < num_blocks - 1)  {
        //printf("Returning block size1 %d (num_blocks %d)\n", block_size, num_blocks);
        return block_size;
    } 
    else {
        //printf("Returning block size2 %d (num_blocks %d)\n", m_count - (block_size * (num_blocks - 1)), num_blocks);
        return m_count - (block_size * (num_blocks - 1));
    } 
}

bool EmberHxMeshGenerator::RingAllreduce::reduceScatter(std::queue<EmberEvent *> &evQ) 
{
    if (m_i > 0 && m_waiting_recv) 
    {
        // we are ready to recv the cycle after we post the recv. In the same cycle, when we post the irecv, the handle is 
        // still not valid as it will be filled when the irecv is executed (so after we return control to emberengine). 
        m_ready_to_recv = true;
        return false;
    }

    if (m_i == 0) 
    {
        if (m_p > 1)
        {
            //printf("[%d] ALLREDUCE: %p; BUFFER: %p\n", m_gen.rank(), this, &(m_dst[0]));
            // here we post the first recv and send the first chunk
            uint32_t recv_block_idx = ((m_r - 2 + m_p) % m_p);
            uint32_t recv_block_size = getBlockSize(recv_block_idx);
            m_gen.enQ_irecv(evQ, &m_tmp[0], recv_block_size, FLOAT, m_recvfrom, m_tag1, m_comm, &m_req_recv);
            DPRINTF("[%d] RS init: recv from %d; recv_block_idx: %d; recv_block_size: %d; tag: %d;\n", m_gen.rank(), m_recvfrom, recv_block_idx, recv_block_size, m_tag1);
            m_waiting_recv = true;

            uint32_t send_block_idx = (m_r - 1 + m_p) % m_p;
            uint32_t send_buff_offset = getBlockOffset(send_block_idx);
            uint32_t send_buff_size = getBlockSize(send_block_idx);
            DPRINTF("[%d] RS init: send to %d; send_block_idx: %d; buff_offset: %d; send_buff_size: %d; tag: %d;\n", m_gen.rank(), m_sendto, send_block_idx, send_buff_offset, send_buff_size, m_tag1);
            m_gen.enQ_isend(evQ, &m_dst[send_buff_offset], send_buff_size, FLOAT, m_sendto, m_tag1, m_comm, &m_req_send);
            m_data_sent += send_buff_size * m_gen.sizeofDataType(FLOAT);
        }
    }
    else 
    {
        // if we are here, it means that the prev recv has been matched. 
        // now we aggregate & forward the recvd block + post the recv for the next one
        m_waiting_recv = false;

        uint32_t recv_block_idx = ((m_r - 1 - m_i + m_p) % m_p);
        uint32_t recv_block_offset = getBlockOffset(recv_block_idx);
        uint32_t recv_block_size = getBlockSize(recv_block_idx);
        //uint32_t recvoffset = ((m_r - 1 - m_i + m_p) % m_p) * m_blocksz;
        //printf("[%d] RS agg: m_i: %d; m_p: %d; recv from %d; recvoffset: %d (%p); m_blocksz: %d\n", m_gen.rank(), m_i, m_p, m_recvfrom, recvoffset, &(m_dst[recvoffset]), m_blocksz);
        aggregate(evQ, &(m_dst[recv_block_offset]), &(m_tmp[0]), recv_block_size);
        
        // cleanup the prev send
        m_gen.enQ_wait(evQ, &m_req_send);
        
        if (m_i < m_p - 1)
        {
            DPRINTF("[%d] RS: send to %d; recv_block_offset: %d; recv_block_offset: %d; count: %d; tag: %d\n", m_gen.rank(), m_sendto, recv_block_idx, recv_block_offset, recv_block_size, m_tag1);
            m_gen.enQ_isend(evQ, &m_dst[recv_block_offset], recv_block_size, FLOAT, m_sendto, m_tag1, m_comm, &m_req_send);
            m_data_sent += recv_block_size * m_gen.sizeofDataType(FLOAT);

            recv_block_idx = ((m_r - m_i - 2 + m_p) % m_p);
            recv_block_size = getBlockSize(recv_block_idx);
            DPRINTF("[%d] RS: recv from %d; recv_block_idx: %d; count: %d; tag: %d\n", m_gen.rank(), m_recvfrom, recv_block_idx, recv_block_size, m_tag1);
            m_gen.enQ_irecv(evQ, &m_tmp[0], recv_block_size, FLOAT, m_recvfrom, m_tag1, m_comm, &m_req_recv);
            m_waiting_recv = true;
        }
    }

    m_i++;

    if (m_i < m_p) return false;
    else {
        m_i = 0;
        return true;
    }
}

bool EmberHxMeshGenerator::RingAllreduce::allGather(std::queue<EmberEvent *> &evQ) 
{
    assert(m_i > 0 || !m_waiting_recv);
    if (m_i > 0 && m_waiting_recv) 
    {
        m_ready_to_recv = true;
        return false;
    }

    if (m_i == 0) 
    {
        if (m_p > 1)
        {
            // here we post the first recv and send the first chunk
            uint32_t recv_block_idx = ((m_r - 1 + m_p) % m_p);
            uint32_t recv_block_size = getBlockSize(recv_block_idx);
            uint32_t recv_block_offset = getBlockOffset(recv_block_idx);
            DPRINTF("[%d] AG init: recv from %d; recv_block_idx: %d; recv_block_offset: %d; recv_block_size: %d; tag: %d;\n", m_gen.rank(), m_recvfrom, recv_block_idx, recv_block_offset, recv_block_size, m_tag2);
            m_gen.enQ_irecv(evQ, &m_dst[recv_block_offset], recv_block_size, FLOAT, m_recvfrom, m_tag2, m_comm, &m_req_recv);
            m_waiting_recv = true;

            uint32_t send_block_idx = ((m_r + m_p) % m_p);
            uint32_t send_block_size = getBlockSize(send_block_idx);
            uint32_t send_block_offset = getBlockOffset(send_block_idx);
            DPRINTF("[%d] AG init: send to %d; send_block_idx: %d; send_block_offset: %d; send_block_size: %d; tag: %d;\n", m_gen.rank(), m_sendto, send_block_idx, send_block_offset, send_block_size, m_tag2);
            m_gen.enQ_isend(evQ, &m_dst[send_block_offset], send_block_size, FLOAT, m_sendto, m_tag2, m_comm, &m_req_send);
            m_data_sent += send_block_size * m_gen.sizeofDataType(FLOAT);
        }
    }
    else 
    {
        // if we are here, it means that the prev recv has been matched. 
        // now we forward the recvd block + post the recv for the next one
        m_waiting_recv = false;
        
        // cleanup the prev send
        m_gen.enQ_wait(evQ, &m_req_send);
        
        if (m_i < m_p - 1)
        {
            uint32_t recv_block_idx = ((m_r - m_i - 1 + m_p) % m_p);
            uint32_t recv_block_size = getBlockSize(recv_block_idx);
            uint32_t recv_block_offset = getBlockOffset(recv_block_idx);
            DPRINTF("[%d] AG: recv from %d; recv_block_idx: %d; recv_block_offset: %d; recv_block_size: %d; tag: %d;\n", m_gen.rank(), m_recvfrom, recv_block_idx, recv_block_offset, recv_block_size, m_tag2);
            m_gen.enQ_irecv(evQ, &m_dst[recv_block_offset], recv_block_size, FLOAT, m_recvfrom, m_tag2, m_comm, &m_req_recv);
            m_waiting_recv = true;

            uint32_t send_block_idx = ((m_r - m_i + m_p) % m_p);
            uint32_t send_block_size = getBlockSize(send_block_idx);
            uint32_t send_block_offset = getBlockOffset(send_block_idx);
            DPRINTF("[%d] AG: send to %d; send_block_idx: %d; send_block_offset: %d; send_block_size: %d; tag: %d;\n", m_gen.rank(), m_sendto, send_block_idx, send_block_offset, send_block_size, m_tag2);
            m_gen.enQ_isend(evQ, &m_dst[send_block_offset], send_block_size, FLOAT, m_sendto, m_tag2, m_comm, &m_req_send);
            m_data_sent += send_block_size * m_gen.sizeofDataType(FLOAT);
        }
    }

    m_i++;

    if (m_i < m_p) return false;
    else {
        m_i = 0;
        return true;
    }
}

/******* RingAllreduceBase *******/
EmberHxMeshGenerator::EmberRingAllreduce::EmberRingAllreduce(EmberHxMeshGenerator &gen, int num_allreduces, uint32_t count, uint32_t rank, uint32_t comm_size, bool nonblocking)
 : m_gen(gen), m_nb(nonblocking), m_has_new_recv(false), m_count(count), m_r(rank), m_p(comm_size)
{
    // tracking active and with-pending-receive allreduces
    m_active_recv_handles.resize(num_allreduces);
    m_active_allreduce_ptrs.resize(num_allreduces);

    // counter of posted recvs
    m_handle_idx = 0;

    // index of last matched recv in m_active_recv_handles
    m_recv_idx = 0;
}

bool EmberHxMeshGenerator::EmberRingAllreduce::progress_phase(std::queue<EmberEvent *> &evQ)
{
    // notify allreduces that recevied a message
    if (m_handle_idx > 0 && m_has_new_recv) 
    {
        m_active_allreduce_ptrs[m_recv_idx]->notifyRecv();
    }

    // progress allreduces
    m_handle_idx = 0;
    m_has_new_recv = false;

    bool all_completed = true;
    for (auto &allreduce : m_allreduces)
    {
        // we don't progress disabled allreduces (they look as completed)
        bool this_completed = !allreduce.isEnabled() || allreduce.progress(evQ);
        all_completed = all_completed && this_completed;

        if (allreduce.hasPendingRecv())
        {
            assert(!this_completed);
            // we need to do this because SST doesn't like invalid recv handles in the waitany :(
            //printf("[%d] allreduce %p waiting for %p\n", m_gen.rank(), &allreduce, allreduce.getRecvHandle());
            m_active_recv_handles[m_handle_idx] = allreduce.getRecvHandle();
            m_active_allreduce_ptrs[m_handle_idx] = &allreduce;
            m_handle_idx++;
        }
    }
    
    if (m_handle_idx > 0)
    {
        if (m_nb) {
            //printf("[%d] testany (m_handle_idx: %d; t: %" PRIu64 ")\n", m_gen.rank(), m_handle_idx, m_gen.getCurrentSimTimeNano());

            // this is needed to advance simtime. Testany doesn't advance it by itself :(
            m_gen.enQ_compute(evQ, 10); 
			m_gen.enQ_testany(evQ, m_handle_idx, &m_active_recv_handles[0], &m_recv_idx, &m_has_new_recv, &m_resp_recv);
        } else {
            m_has_new_recv = true;
            m_gen.enQ_waitany(evQ, m_handle_idx, &m_active_recv_handles[0], &m_recv_idx, &m_resp_recv);
        }
        
        return false;
    }
    
    return all_completed;
}

void EmberHxMeshGenerator::EmberRingAllreduce::printStats() 
{
    uint64_t rank_time = m_stop_time - m_start_time;
    uint64_t bytes = m_count * m_gen.sizeofDataType(FLOAT);
    uint64_t data_moved = 0;
    for (auto &allreduce : m_allreduces) data_moved += allreduce.getMovedBytes();
    double bw = (double) 8*data_moved / rank_time;
    double gbw = bw * m_p;
    
    //printf("Size %d - Start %" PRIu64 " - Stop %" PRIu64 " - Diff %" PRIu64 " - Count %d - JobId %d\n", m_gen.size(), m_start_time, m_stop_time, m_stop_time - m_start_time, m_count, m_gen.getJobId());
    printf("TIME %d %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %lf %lf\n", m_r, m_start_time, m_stop_time, rank_time, bytes, data_moved, bw, gbw);
}

/******* RingAllreduce0.5D *******/
EmberHxMeshGenerator::EmberRingAllreduce05D::EmberRingAllreduce05D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, RingType ring_type)
: EmberRingAllreduce(gen, 1, count, rank, comm_size, nb), m_state(INIT)
{
    // Coordinates
    uint32_t sendto = ((m_r + 1) % m_p);
    uint32_t recvfrom = ((m_r - 1 + m_p) % m_p);

    // Buffer
#ifdef VALIDATE
    m_dstx_1 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count));
    assert(m_dstx_1 != NULL);
    for (int i=0; i<m_count; i++)
    {
        m_dstx_1[i] = i;
    }
#else
    m_dstx_1 = NULL;
#endif

    // Allreduces
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dstx_1, m_count, m_r, m_p, recvfrom, sendto, aggregation_cost_ns, comm));

    printf("Rank: %d; Grid: %dx1; m_count: %d; sendto: %d; recvfrom: %d; Blocking: %d\n", m_r, m_p, m_count, sendto, recvfrom, (int) m_nb);
}

bool EmberHxMeshGenerator::EmberRingAllreduce05D::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT:
        m_gen.enQ_getTime(evQ, &m_start_time);
        m_state = PHASE_1;
        // no break/return needed here
    case PHASE_1: 
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
#ifdef VALIDATE
        validate();
#endif
        m_gen.enQ_getTime(evQ, &m_stop_time);
        m_state = FINI;
        return false;
    case FINI:
        // we do one more cycle to make sure enQ_getTime completes before somebody calls printStats()
        //m_state = INIT;
        //for (auto &allreduce : m_allreduces) allreduce.reset();
        return true;
    default:
        assert(0);
    }
    assert(0);
}

void EmberHxMeshGenerator::EmberRingAllreduce05D::validate()
{
#ifdef DEBUG
    printf("[%d] res: ", m_gen.rank());
    for (int i=0; i < m_count; i++) {
        printf("%f ", m_dstx_1[i]);
    }
    printf("\n");
#endif 
    for (int i = 0; i < m_count; i++)
    {
        assert(m_dstx_1[i] == i * m_p);
    }
}

void EmberHxMeshGenerator::EmberRingAllreduce05D::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberHxMeshGenerator::EmberRingAllreduce05D::~EmberRingAllreduce05D()
{
#ifdef VALIDATE
    free(m_dstx_1);
#endif
}


/******* RingAllreduce1D *******/
EmberHxMeshGenerator::EmberRingAllreduce1D::EmberRingAllreduce1D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, RingType ring_type)
: EmberRingAllreduce(gen, 2, count, rank, comm_size, nb), m_state(INIT)
{
    // Coordinates
    uint32_t sendto = ((m_r + 1) % m_p);
    uint32_t recvfrom = ((m_r - 1 + m_p) % m_p);

    // Buffer
#ifdef VALIDATE
    m_dstx_1 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 2));
    m_dstx_2 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 2));
    assert(m_dstx_1 != NULL && m_dstx_2 != NULL);
    for (int i=0; i<m_count/2; i++)
    {
        m_dstx_1[i] = i;
        m_dstx_2[i] = m_count/2 + i;
    }
#else
    m_dstx_1 = NULL;
    m_dstx_2 = NULL;
#endif

    // Allreduces
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dstx_1, m_count/2, m_r      , m_p, recvfrom, sendto,   aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dstx_2, m_count/2, m_p - m_r, m_p, sendto,   recvfrom, aggregation_cost_ns, comm));

    // Checks
    assert((m_count / 2) * 2 == m_count);

    printf("Rank: %d; Grid: %dx1; m_count: %d; sendto: %d; recvfrom: %d; Blocking: %d\n", m_r, m_p, m_count, sendto, recvfrom, (int) m_nb);
}

bool EmberHxMeshGenerator::EmberRingAllreduce1D::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT:
        m_gen.enQ_getTime(evQ, &m_start_time);
        m_state = PHASE_1;
        // no break/return needed here
    case PHASE_1: 
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
#ifdef VALIDATE
        validate();
#endif
        m_gen.enQ_getTime(evQ, &m_stop_time);
        m_state = FINI;
        return false;
    case FINI:
        // we do one more cycle to make sure enQ_getTime completes before somebody calls printStats()
        //m_state = INIT;
        //for (auto &allreduce : m_allreduces) allreduce.reset();
        return true;
    default:
        assert(0);
    }
    assert(0);
}

void EmberHxMeshGenerator::EmberRingAllreduce1D::validate()
{
#ifdef DEBUG
    printf("[%d] res: ", m_gen.rank());
    for (int i=0; i < m_count / 2; i++) {
        printf("%f ", m_dstx_1[i]);
    }

    for (int i=0; i < m_count / 2; i++) {
        printf("%f ", m_dstx_2[i]);
    }

    printf("\n");
#endif 
    for (int i = 0; i < m_count / 2; i++)
    {
        //assert(m_dstx_1[i] == m_p);
        //assert(m_dstx_2[i] == m_p);
    }
}

void EmberHxMeshGenerator::EmberRingAllreduce1D::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberHxMeshGenerator::EmberRingAllreduce1D::~EmberRingAllreduce1D()
{
#ifdef VALIDATE
    free(m_dstx_1);
    free(m_dstx_2);
#endif
}

/******* RingAllreduce2D *******/
EmberHxMeshGenerator::EmberRingAllreduce2D::EmberRingAllreduce2D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, uint32_t px, RingType ring_type)
: EmberRingAllreduce(gen, 4, count, rank, comm_size, nb), m_state(INIT)
{
    int dims[2] = {0, 0};

    // If we don't define a custom px then just use the one from mpich
    if (px == 0) {
        int res = mpich_dims_create(comm_size, 2, dims);
        assert(res==0);
    } else {
        dims[0] = px;
        dims[1] = m_gen.size() / px;
        assert(m_gen.size() % px == 0);
    }

    m_px = dims[0];
    m_py = dims[1];
    printf("m_px %d - m_py %d \n", m_px, m_py);

    // Coordinates
    uint32_t rowstart = (m_r / m_px) * m_px;
    uint32_t sendto_x = rowstart + ((m_r + 1) % m_px);
    uint32_t recvfrom_x = rowstart + ((m_r - 1 + m_px) % m_px);
    uint32_t sendto_y = (m_r + m_px) % m_p;
    uint32_t recvfrom_y = (m_r - m_px + m_p) % m_p;
    uint32_t rx = m_r / m_px;
    uint32_t ry = m_r % m_px;

    // Buffers
#ifdef VALIDATE
    m_dstx_1 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dstx_2 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dsty_1 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dsty_2 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    assert(m_dstx_1 != NULL && m_dstx_2 != NULL);
    assert(m_dsty_1 != NULL && m_dsty_2 != NULL);
    for (int i=0; i<m_count/4; i++)
    {
        m_dstx_1[i] = i;
        m_dstx_2[i] = m_count/4 + i;
        m_dsty_1[i] = m_count/2 + i;
        m_dsty_2[i] = 3*m_count/4 + i;
    }
#else
    m_dstx_1 = NULL; 
    m_dstx_2 = NULL;
    m_dsty_1 = NULL;
    m_dsty_2 = NULL;
#endif

    // Allreduces
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dstx_1, m_count/4, ry       , m_px, recvfrom_x, sendto_x,   aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dstx_2, m_count/4, m_px - ry, m_px, sendto_x,   recvfrom_x, aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dsty_1, m_count/4, rx       , m_py, recvfrom_y, sendto_y,   aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dsty_2, m_count/4, m_py - rx, m_py, sendto_y,   recvfrom_y, aggregation_cost_ns, comm));

    // Checks
    assert(m_px * m_py == m_p);
    assert((m_count / 4) * 4 == m_count);

    printf("Rank: %d; Grid: %dx%d; m_count: %d; sendto_x: %d; recvfrom_x: %d; sendto_y: %d; recvfrom_y: %d; Blocking: %d\n", m_r, m_py, m_px, m_count, sendto_x, recvfrom_x, sendto_y, recvfrom_y, (int) !m_nb);
}

bool EmberHxMeshGenerator::EmberRingAllreduce2D::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT:
        m_gen.enQ_getTime(evQ, &m_start_time);
        m_state = PHASE_1;
        // no break/return needed here
    case PHASE_1: 
        if (progress_phase(evQ)) 
        {
            // swap buffers
            m_allreduces[0].setBuff(m_dsty_1);
            m_allreduces[1].setBuff(m_dsty_2);
            m_allreduces[2].setBuff(m_dstx_1);
            m_allreduces[3].setBuff(m_dstx_2);
            
            for (auto &allreduce : m_allreduces) allreduce.reset();

            m_state = PHASE_2;
        } else return false;
    case PHASE_2:
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
#ifdef VALIDATE
        validate();
#endif
        m_gen.enQ_getTime(evQ, &m_stop_time);
        m_state = FINI;
        //return false;
    case FINI:
        // we do one more cycle to make sure enQ_getTime completes before somebody calls printStats()
        //m_state = INIT;
        //for (auto &allreduce : m_allreduces) allreduce.reset();
        return true;
    default:
        assert(0);
    }
    assert(0);
}

void EmberHxMeshGenerator::EmberRingAllreduce2D::validate()
{
#ifdef DEBUG
    printf("[%d] res: ", m_gen.rank());
    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dstx_1[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dstx_2[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dsty_1[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dsty_2[i]);
    }

    printf("\n");
#endif

    for (int i = 0; i < m_count / 4; i++)
    {
        assert(m_dstx_1[i] == i*m_p);
        assert(m_dstx_2[i] == (m_count/4 + i) * m_p);
        assert(m_dsty_1[i] == (m_count/2 + i) * m_p);
        assert(m_dsty_2[i] == (3*m_count/4 + i) * m_p);
    }
}

void EmberHxMeshGenerator::EmberRingAllreduce2D::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberHxMeshGenerator::EmberRingAllreduce2D::~EmberRingAllreduce2D()
{
#ifdef VALIDATE
    free(m_dstx_1);
    free(m_dstx_2);
    free(m_dsty_1);
    free(m_dsty_2);
#endif
}

/******* RingAllreduce25D *******/
EmberHxMeshGenerator::EmberRingAllreduce25D::EmberRingAllreduce25D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, uint32_t px, RingType ring_type)
: EmberRingAllreduce(gen, 4, count, rank, comm_size, nb), m_state(INIT)
{
    int dims[2] = {0, 0};

    // If we don't define a custom px then just use the one from mpich
    if (px == 0) {
        int res = mpich_dims_create(comm_size, 2, dims);
        assert(res==0);
    } else {
        dims[0] = m_gen.size() / px;
        dims[1] = px;
        printf("Setting Custom Px! dims[1]  %d - dims[0] %d\n", dims[1], dims[0]); fflush(stdout);
        assert(m_gen.size() % px == 0);
        assert(dims[0] % dims[1] == 0);
    }
    
    k0 = dims[1];
    k1 = dims[0];
    
    printf("K0(px) is %d - K1 is %d - Size %d - Job ID %d", k0, k1,m_gen.size(), m_gen.getJobId()); fflush(stdout);
    auto neighs0 = getNeighRing0(rank);
    auto neighs1 = getNeighRing1(rank);
    uint neigh0a = neighs0.first;
    uint neigh0b = neighs0.second;
    uint neigh1a = neighs1.first;
    uint neigh1b = neighs1.second;
    /*assert(neigh0a != neigh0b && neigh0a != neigh1a && neigh0a != neigh1b &&
           neigh0b != neigh1a && neigh0b != neigh1b &&
           neigh1a != neigh1b);
    assert(neigh0a != neigh0b);
    assert(neigh0a != neigh1a);
    assert(neigh0a != neigh1b);
    assert(neigh0b != neigh1a);
    assert(neigh0b != neigh1b);
    assert(neigh1a != neigh1b);*/

    // Buffer
#ifdef VALIDATE
    m_dst_1 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dst_2 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dst_3 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    m_dst_4 = (float*) malloc(gen.sizeofDataType(FLOAT)*(m_count / 4));
    assert(m_dst_1 != NULL && m_dst_2 != NULL && m_dst_3 != NULL && m_dst_4 != NULL);
    for (int i=0; i<m_count/4; i++)
    {
        m_dst_1[i] = i;
        m_dst_2[i] = 2*i;
        m_dst_3[i] = 3*i;
        m_dst_4[i] = 4*i;
    }
#else
    m_dst_1 = NULL;
    m_dst_2 = NULL;
    m_dst_3 = NULL;
    m_dst_4 = NULL;
#endif

    // Allreduces
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dst_1, m_count/4, m_r      , m_p, neigh0a, neigh0b, aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dst_2, m_count/4, m_p - m_r, m_p, neigh0b, neigh0a, aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dst_3, m_count/4, m_r      , m_p, neigh1a, neigh1b, aggregation_cost_ns, comm));
    m_allreduces.push_back(RingAllreduce(m_gen, ring_type, m_dst_4, m_count/4, m_p - m_r, m_p, neigh1b, neigh1a, aggregation_cost_ns, comm));

    // Checks
    assert(k0*k1 == m_p);
    //printf("M_COUNT is %d \n", m_count);
    assert((m_count / 4) * 4 == m_count);

    printf("Rank: %d; Grid: %dx%d; m_count: %d; neigh0a: %d; neigh0b: %d; neigh1a: %d; neigh1b: %d; Blocking: %d\n", m_r, k0, k1, m_count, neigh0a, neigh0b, neigh1a, neigh1b, (int) m_nb);
}

bool EmberHxMeshGenerator::EmberRingAllreduce25D::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT:
        m_gen.enQ_getTime(evQ, &m_start_time);
        m_state = PHASE_1;
        // no break/return needed here
    case PHASE_1: 
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
#ifdef VALIDATE
        validate();
#endif
        m_gen.enQ_getTime(evQ, &m_stop_time);
        m_state = FINI;
        return false;
    case FINI:
        // we do one more cycle to make sure enQ_getTime completes before somebody calls printStats()
        //m_state = INIT;
        //for (auto &allreduce : m_allreduces) allreduce.reset();
        return true;
    default:
        assert(0);
    }
    assert(0);
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getRowColFromRank(uint rank){
    return std::pair<uint, uint>(rank / k0, rank % k0);
}

uint EmberHxMeshGenerator::EmberRingAllreduce25D::getRankFromRowCol(std::pair<uint, uint> rowcol){
    return rowcol.first * k0 + rowcol.second;
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getx1x0(uint X){
    //return std::pair<uint, uint>(std::floor(X / k1), X % k0);
    return std::pair<uint, uint>(X / k0, X % k0);
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getf0(uint X){
    auto x1x0 = getx1x0(X);
    uint x1 = x1x0.first;
    uint x0 = x1x0.second;
    return std::pair<uint, uint>(x1 % k1, (x0 + (k0 - 1) * x1) % k0);
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getf1(uint X){
    auto x1x0 = getx1x0(X);
    uint x1 = x1x0.first;
    uint x0 = x1x0.second;
    return std::pair<uint, uint>((x0 + (k0 - 1) * x1) % k1, x1 % k0);
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getNeighRing0(uint rank){
    auto rowcol = getRowColFromRank(rank);
    uint row = rowcol.first;
    uint col = rowcol.second;
    uint Xrank = k0*k1 + 1; // The X such that f0(X) == rank
    for(uint X = 0; X < k0*k1; X++){
        auto f0 = getf0(X);
        //printf("f0.first %d row %d - f0.second %d col %d\n",f0.first, row, f0.second, col); fflush(stdout);
        if(f0.first == row && f0.second == col){
            Xrank = X;
            break;
        }
    }
    assert(Xrank != k0*k1 + 1);
    uint Xpre, Xpost;
    if(Xrank == 0){
        Xpre = k0*k1 - 1;
    }else{
        Xpre = Xrank - 1;
    }
    Xpost = (Xrank + 1) % (k0*k1);
    return std::pair<uint, uint>(getRankFromRowCol(getf0(Xpre)), getRankFromRowCol(getf0(Xpost)));
}

std::pair<uint, uint> EmberHxMeshGenerator::EmberRingAllreduce25D::getNeighRing1(uint rank){
    auto rowcol = getRowColFromRank(rank);
    uint row = rowcol.first;
    uint col = rowcol.second;
    uint Xrank = k0*k1 + 1; // The X such that f1(X) == rank
    for(uint X = 0; X < k0*k1; X++){
        auto f1 = getf1(X);
        if(f1.first == row && f1.second == col){
            Xrank = X;
            break;
        }
    }
    assert(Xrank != k0*k1 + 1);
    uint Xpre, Xpost;
    if(Xrank == 0){
        Xpre = k0*k1 - 1;
    }else{
        Xpre = Xrank - 1;
    }
    Xpost = (Xrank + 1) % (k0*k1);
    return std::pair<uint, uint>(getRankFromRowCol(getf1(Xpre)), getRankFromRowCol(getf1(Xpost)));
}

void EmberHxMeshGenerator::EmberRingAllreduce25D::validate()
{
#ifdef DEBUG
    printf("[%d] res: ", m_gen.rank());
    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dst_1[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dst_2[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dst_3[i]);
    }

    for (int i=0; i < m_count / 4; i++) {
        printf("%f ", m_dst_4[i]);
    }

    printf("\n");
#endif 
    for (int i = 0; i < m_count / 4; i++)
    {
        assert(m_dst_1[i] == i*m_p);
        assert(m_dst_2[i] == (m_count/4 + i) * m_p);
        assert(m_dst_3[i] == (m_count/2 + i) * m_p);
        assert(m_dst_4[i] == (3*m_count/4 + i) * m_p);
    }
}

void EmberHxMeshGenerator::EmberRingAllreduce25D::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberHxMeshGenerator::EmberRingAllreduce25D::~EmberRingAllreduce25D()
{
#ifdef VALIDATE
    free(m_dst_1);
    free(m_dst_2);
    free(m_dst_3);
    free(m_dst_4);    
#endif
}

/******* RingAllreduceRev *******/
// Convert a rank id into a list of d-dimensional coordinates
static void getCoordFromId(int id, uint* dimensions, uint dimensions_num, int* coord, int m_p){
    int nnodes = m_p;
    for (int i = 0; i < dimensions_num; i++) {
        nnodes = nnodes / dimensions[i];
        coord[i] = id / nnodes;
        id = id % nnodes;
    }
}

// Convert d-dimensional coordinates into a rank id
static int getIdFromCoord(int* coords, uint* dimensions, uint dimensions_num){
    int rank = 0;
    int multiplier = 1;
    int coord;
    for (int i = dimensions_num - 1; i >= 0; i--) {
        coord = coords[i];
        if (/*cart_ptr->topo.cart.periodic[i]*/ 1) {
            if (coord >= dimensions[i])
                coord = coord % dimensions[i];
            else if (coord < 0) {
                coord = coord % dimensions[i];
                if (coord)
                    coord = dimensions[i] + coord;
            }
        }
        rank += multiplier * coord;
        multiplier *= dimensions[i];
    }
    return rank;
}
static int mod(int a, int b){
    int r = a % b;
    return r < 0 ? r + b : r;
}
EmberHxMeshGenerator::EmberRingAllreduceRev::EmberRingAllreduceRev(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, uint dimensions, uint* dimensions_sizes, RingType ring_type, float* data)
: EmberRingAllreduce(gen, 2*dimensions*2*dimensions, count, rank, comm_size, nb), m_state(0), m_dimensions(dimensions), m_dimensions_sizes(dimensions_sizes), m_validate(data!=NULL), m_data(data)
{    
    assert(ring_type == RING_ALLREDUCE);

    printf("\nUsing 2dRev2\n"); fflush(stdout);

    printf("Being called with dimension %d, count %d, %d\n", m_dimensions, count, "a");fflush(stdout);

    // Adjust count 
    while(count % (2*dimensions)){
        count += 1;
    }
    m_count = count;

    uint32_t relcoord[MAX_SUPPORTED_DIMENSIONS];
    uint32_t recvfrom[MAX_SUPPORTED_DIMENSIONS];
    uint32_t sendto[MAX_SUPPORTED_DIMENSIONS];

    if(dimensions_sizes == NULL){
        dimensions_sizes = (uint*) malloc(sizeof(uint)*MAX_SUPPORTED_DIMENSIONS);
        if(dimensions == 1){
            dimensions_sizes[0] = m_p;
            DPRINTF("Considering a logical topology of size %d\n", dimensions_sizes[0]);
        }else if(dimensions == 2){
            dimensions_sizes[0] = sqrt(m_p);
            dimensions_sizes[1] = sqrt(m_p);
            DPRINTF("Considering a logical topology of size %dx%d\n", dimensions_sizes[0], dimensions_sizes[1]);
        }else if(dimensions == 3){
            dimensions_sizes[0] = cbrt(m_p);
            dimensions_sizes[1] = cbrt(m_p);
            dimensions_sizes[2] = cbrt(m_p);
            DPRINTF("Considering a logical topology of size %dx%dx%d\n", dimensions_sizes[0], dimensions_sizes[1], dimensions_sizes[2]);
        }else{
            fprintf(stderr, "%d dimensions not supported.", dimensions);
            exit(-1);
        }
        m_dimensions_sizes = dimensions_sizes;
    }
    for(size_t i = 0; i < dimensions; i++){
        int coord[MAX_SUPPORTED_DIMENSIONS];
        getCoordFromId(m_r, m_dimensions_sizes, dimensions, coord, m_p);
        relcoord[i] = coord[i];

        coord[i] = (relcoord[i] + 1) % m_dimensions_sizes[i];
        sendto[i] = getIdFromCoord(coord, m_dimensions_sizes, dimensions);

        coord[i] = mod((relcoord[i] - 1), m_dimensions_sizes[i]);
        recvfrom[i] = getIdFromCoord(coord, m_dimensions_sizes, dimensions);
    }
    

    size_t data_size[MAX_SUPPORTED_DIMENSIONS];
    size_t offsets[MAX_SUPPORTED_DIMENSIONS];
    for(int i = 0; i < m_dimensions; i++){
        data_size[i] = m_count/(2*m_dimensions);
        offsets[i] = 0;
    }    

    std::vector<RingAllreduce> reducescatters, allgathers;

    /**********************************************************/
    /* Prepare reducescatter and allgater in the wrong order. */
    /**********************************************************/
    for(int s = 0; s < m_dimensions; s++){
        for(int i = 0; i < m_dimensions; i++){
            int current_d = (i + s) % m_dimensions;
            size_t offset_l = (2*i)*(m_count/(2*m_dimensions))   + offsets[i];
            size_t offset_r = (2*i+1)*(m_count/(2*m_dimensions)) + offsets[i];
            assert(offset_l + data_size[i] <= (2*i+1)*(m_count/(2*m_dimensions)));
            
            /*
            if(i == 0){
                printf("[%d] s: %d i: %d offset_l: %d offset_r: %d\n", m_r, s, i, offset_l, offset_r);
            }
            */
        
            reducescatters.push_back(RingAllreduce(m_gen, RING_REDUCE_SCATTER, data + offset_l, data_size[i], relcoord[current_d]                                    , m_dimensions_sizes[current_d], recvfrom[current_d], sendto[current_d]  , aggregation_cost_ns, comm, m_validate));    
            if(m_validate){
                reducescatters.push_back(RingAllreduce(m_gen, RING_REDUCE_SCATTER, data + offset_r, data_size[i], relcoord[current_d]                                    , m_dimensions_sizes[current_d], recvfrom[current_d], sendto[current_d]  , aggregation_cost_ns, comm, m_validate));    
            }else{
                reducescatters.push_back(RingAllreduce(m_gen, RING_REDUCE_SCATTER, data + offset_r, data_size[i], m_dimensions_sizes[current_d] - relcoord[current_d] - 1, m_dimensions_sizes[current_d], sendto[current_d]  , recvfrom[current_d], aggregation_cost_ns, comm, m_validate));    
            }            

            allgathers.push_back(RingAllreduce(m_gen, RING_ALLGATHER, data + offset_l, data_size[i], relcoord[current_d]                                    , m_dimensions_sizes[current_d], recvfrom[current_d], sendto[current_d]  , aggregation_cost_ns, comm, m_validate));    
            if(m_validate){
                allgathers.push_back(RingAllreduce(m_gen, RING_ALLGATHER, data + offset_r, data_size[i], relcoord[current_d]                                    , m_dimensions_sizes[current_d], recvfrom[current_d], sendto[current_d]  , aggregation_cost_ns, comm, m_validate));    
            }else{
                allgathers.push_back(RingAllreduce(m_gen, RING_ALLGATHER, data + offset_r, data_size[i], m_dimensions_sizes[current_d] - relcoord[current_d] - 1, m_dimensions_sizes[current_d], sendto[current_d]  , recvfrom[current_d], aggregation_cost_ns, comm, m_validate));    
            }
            if(s != m_dimensions - 1){
                data_size[i] /= m_dimensions_sizes[current_d];
                if(m_validate){
                    offsets[i] += relcoord[current_d]*data_size[i];
                }
            }
        }
    }

    // Put them in the correct order
    for(int i = 0; i < m_dimensions*m_dimensions*2; i++){
        m_allreduces.push_back(reducescatters[i]);
    }
    for(int i = m_dimensions*m_dimensions*2 - 1; i >= 0; i--){
        m_allreduces.push_back(allgathers[i]);
    }

    // Checks
    assert((m_count / (m_dimensions*2)) * (m_dimensions*2) == m_count);
}

bool EmberHxMeshGenerator::EmberRingAllreduceRev::progress(std::queue<EmberEvent *> &evQ)
{
    while(true){
        if(m_state <= 2*m_dimensions){
            if(m_state == 0){
                m_gen.enQ_getTime(evQ, &m_start_time);

                // all dimensions same size: d = m_dimensions_sizes[0]
                const uint64_t d = (uint64_t)m_dimensions_sizes[0];
                const uint64_t total_steps = 2ULL * (uint64_t)m_dimensions * (d - 1ULL);
                const uint64_t total_latency_ns = total_steps *1500ULL;

                m_gen.enQ_compute(evQ, total_latency_ns);
            }else{
                m_gen.enQ_getTime(evQ, &m_stop_time);
                bool completed = progress_phase(evQ);
                if (!completed) return false;  
            }
            for(size_t stage = 0; stage < 2*m_dimensions; stage++){
                for(size_t c = 0; c < m_dimensions*2; c++){
                    bool v;
                    if(stage == m_state){
                        v = true;
                    }else{
                        v = false;
                    }
                    m_allreduces[stage*m_dimensions*2 + c].setEnable(v);
                }
            }       
            m_state += 1;        
        }else{ // FINI            
            return true;
        }
    }
}

void EmberHxMeshGenerator::EmberRingAllreduceRev::validate()
{
    assert(0);
}

void EmberHxMeshGenerator::EmberRingAllreduceRev::reset()
{
    m_state = 0;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberHxMeshGenerator::EmberRingAllreduceRev::~EmberRingAllreduceRev()
{
    ;
}

/******* Alltoall (randomized) *******/
EmberHxMeshGenerator::EmberRandAlltoall::EmberRandAlltoall(EmberHxMeshGenerator &gen, uint32_t sendcount, uint32_t numblocks, uint32_t rank, uint32_t comm_size, Communicator comm, bool nb, bool det)
: m_gen(gen), m_r(rank), m_p(comm_size), m_sendcount(sendcount), m_comm(comm), m_numblocks(numblocks), m_tot_blocks(0), m_tag(0), m_send_block_idx(NULL), m_it(0)
{
    assert((sendcount/numblocks)*numblocks == sendcount);

    XORShiftRNG* rng = new XORShiftRNG(comm_size + m_r);

    m_tot_blocks = m_numblocks * m_p;
    m_send_block_idx = (int*) malloc(sizeof(int) * m_tot_blocks);
    assert(m_send_block_idx!=NULL);

    for (int i=0; i<m_tot_blocks; i++) m_send_block_idx[i] = i;

    // generate random permutation
    if (det) {
        for (int i = m_tot_blocks-1; i >= 0; --i){
            //generate a random number [0, n-1]
            int j = rng->generateNextUInt32() % (i+1);

            //swap the last element with element at random index
            int temp = m_send_block_idx[i];
            m_send_block_idx[i] = m_send_block_idx[j];
            m_send_block_idx[j] = temp;
        }
    }

    m_tag = m_gen.getNextTag();

    int blocks_to_exchange = (m_p - 1)*m_numblocks;
    m_req_recv = (MessageRequest *) malloc(sizeof(MessageRequest) * blocks_to_exchange);
    m_req_send = (MessageRequest *) malloc(sizeof(MessageRequest) * blocks_to_exchange);
}

EmberHxMeshGenerator::EmberRandAlltoall::~EmberRandAlltoall()
{
    free(m_send_block_idx);
    free(m_req_recv);
    free(m_req_send);
}

void EmberHxMeshGenerator::EmberRandAlltoall::reset()
{
    m_it = 0;
    m_tag = m_gen.getNextTag();
}

bool EmberHxMeshGenerator::EmberRandAlltoall::progress(std::queue<EmberEvent *> &evQ)
{        
    int blocks_to_exchange = (m_p - 1)*m_numblocks;

    if (m_it == 0) 
    {
        printf("[%d] m_it: %d\n", m_r, m_it);
        uint32_t block_size = m_sendcount / m_numblocks;
        uint32_t recvs = 0;
        for (int i=1; i<=blocks_to_exchange; i++)
        {
            int recvfrom = (m_r + (i + m_numblocks - 1) / m_numblocks) % m_p;
            DPRINTF("[%d] recv from %d; block_size: %d\n", m_r, recvfrom, block_size);
            m_gen.enQ_irecv(evQ, NULL, block_size, FLOAT, recvfrom, m_tag, m_comm, &m_req_recv[recvs]);
            recvs++;
        }
        int sends = 0;
        for (int i=0; i<m_tot_blocks; i++)
        {
            int block_idx = m_send_block_idx[i];
            int sendto = block_idx / m_numblocks;
            if (sendto != m_r) {
                DPRINTF("[%d] sendto: %d; block_idx: %d; block_size: %d\n", m_r, sendto, block_idx, block_size);
                m_gen.enQ_isend(evQ, NULL, block_size, FLOAT, sendto, m_tag, m_comm, &m_req_send[sends]);
                sends++;
            }
        }
        assert(recvs == blocks_to_exchange);
        assert(sends == blocks_to_exchange);
        m_it++;
        return false;
    }
    else if (m_it == 1)
    {
        m_gen.enQ_waitall(evQ, blocks_to_exchange, m_req_recv);
        m_gen.enQ_getTime(evQ, &m_time_debug);
        m_it++;
        return false;
    }
    else if (m_it == 2)
    {
        DPRINTF("[%d] %" PRIu64 " wait recvs is over\n", m_r, m_time_debug);
        m_gen.enQ_waitall(evQ, blocks_to_exchange, m_req_send);
        m_gen.enQ_getTime(evQ, &m_time_debug);
        m_it++;
        return false;
    } 
    else 
    {
        DPRINTF("[%d] %" PRIu64 " wait sends is over\n", m_r, m_time_debug);
        m_gen.enQ_barrier(evQ, m_comm);
        return true;
    }
    assert(0);
}


/******* AllToAllImproved *******/
int EmberHxMeshGenerator::EmberImprovedAlltoall::mod(int a, int b) {
    int tmp = ((a % b) + b) % b;
    return tmp;
}

EmberHxMeshGenerator::EmberImprovedAlltoall::~EmberImprovedAlltoall()
{
    free(msgRequests);
}

void EmberHxMeshGenerator::EmberImprovedAlltoall::reset()
{
    m_loopIndex = 0;
    nextMRIndex = 0;
    m_tag = m_gen.getNextTag();
}

EmberHxMeshGenerator::EmberImprovedAlltoall::EmberImprovedAlltoall(EmberHxMeshGenerator &gen, uint32_t sendcount, uint32_t rank, uint32_t comm_size, Communicator comm)
: m_gen(gen), m_r(rank), m_p(comm_size), m_sendcount(sendcount), m_comm(comm), m_tag(0)
{
    m_sendBuf = NULL;
    m_recvBuf = NULL;

    m_tag = m_gen.getNextTag();

    nextMRIndex = 0;
    msgRequests = (MessageRequest*) malloc(sizeof(MessageRequest) * (comm_size) * 2);
}


bool EmberHxMeshGenerator::EmberImprovedAlltoall::progress(std::queue<EmberEvent *> &evQ)
{        
    if ( m_loopIndex == m_iterations ) {
        m_gen.enQ_waitall( evQ, nextMRIndex, msgRequests, NULL );
        return true;
    }

    const bool participate = true;
    if( participate ) {

        for (int i = 1; i <= m_p; i++) {
            if (mod(m_r - i, m_p) != m_r) {
                m_gen.enQ_irecv( evQ, NULL, m_sendcount, FLOAT,
                    mod(m_r - i, m_p), m_tag, m_comm, &msgRequests[nextMRIndex++]);
            }
        }

        for (int i = 1; i <= m_p; i++) {
            if (mod(m_r + i, m_p) != m_r) {
                m_gen.enQ_isend( evQ, NULL, m_sendcount, FLOAT, mod(m_r + i, m_p), m_tag,
                                                    m_comm, &msgRequests[nextMRIndex++]);
            }
        }
        m_loopIndex++;
    }
    return false;
}

/******* EmberHxMeshGenerator (parent Ember motif) *******/
EmberHxMeshGenerator::EmberHxMeshGenerator(SST::ComponentId_t id, Params &params)
: EmberMessagePassingGenerator(id, params, "None"), m_tag(UINT_MAX)
{
    assert(0);
} 

EmberHxMeshGenerator::EmberHxMeshGenerator(SST::ComponentId_t id, Params &params, std::string name) 
: EmberMessagePassingGenerator(id, params, name), m_tag(UINT_MAX)
{
    // we want to exchange data for real
#ifdef VALIDATE
    memSetBacked();
#endif
}

bool EmberHxMeshGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    assert(0);
}
