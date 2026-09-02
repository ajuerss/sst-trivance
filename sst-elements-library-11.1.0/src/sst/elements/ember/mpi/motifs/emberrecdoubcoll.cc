#include <sst_config.h>
#include <sst/core/rng/xorshift.h>
#include <limits.h>
#include "emberrecdoubcoll.h"
#include <cmath>

//#define DEBUG

#define MAX_SUPPORTED_DIMENSIONS 8 // We support up to 8D torus

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

static int mod(int a, int b){
    int r = a % b;
    return r < 0 ? r + b : r;
}

EmberRecDoubCollGenerator::RecDoubCollectiveEngine::RecDoubCollectiveEngine(EmberRecDoubCollGenerator &gen, CollType coll_type, uint* dimensions, uint dimensions_num,
                                                                      float *dst, uint32_t count, uint32_t vrank, uint32_t numproc, double aggregation_cost_ns, 
                                                                      Communicator comm, bool validate, uint port, bool latency_optimal)
    :  m_gen(gen), m_count(count), m_dst(dst), m_r(vrank), m_p(numproc), m_aggregation_cost_ns(aggregation_cost_ns), m_data_sent(0), 
       m_comm(comm), m_dimensions(dimensions), m_dimensions_num(dimensions_num), m_validate(validate), m_port(port), m_latency_optimal(latency_optimal), m_enabled(true)
{
    // TODO What if m_count < m_p or not divisible for m_p ???
    uint32_t block_size = (m_count >= m_p) ? m_count / m_p : m_count;

    if(m_validate){
        m_tmp = (float*) malloc(gen.sizeofDataType(FLOAT)*m_count); // Even though at most each rank can receive a block of m_count/2
        assert(m_tmp!=NULL);
        m_send_tmp = (float*) malloc(gen.sizeofDataType(FLOAT)*m_count); // Even though at most each rank can send a block of m_count/2
        assert(m_send_tmp!=NULL);
    }else{
        m_tmp = NULL;
        m_send_tmp = NULL;
    }

    m_blocks_bitmap_s = (uint8_t*) malloc(m_p*sizeof(uint8_t));
    m_blocks_bitmap_r = (uint8_t*) malloc(m_p*sizeof(uint8_t));

   switch (coll_type) {
    case RECDOUB_ALLREDUCE:
        m_do_reduce_scatter = true;
        if(m_count >= m_p && !m_latency_optimal){
            m_do_allgather = true;
        }else{
            if(m_r == 0){
                DPRINTF("[%d] Skipping allgather (msg too small, or latency optimal required, \"reducescatter\" will be enough to do allreduce).\n", m_r);
            }
            m_do_allgather = false; // No need to do allgather for too small messages (all the data will be sent at each step)
        }
        break;
    case RECDOUB_REDUCE_SCATTER:
        m_do_reduce_scatter = true;
        m_do_allgather = false;
        break;
    case RECDOUB_ALLGATHER:
        m_do_reduce_scatter = false;
        m_do_allgather = true;
        break;
    default: assert(0);
    }

    reset();
    printf("Count is %d - m_p is %d - block size %d\n",count, m_p, block_size); fflush(stdout);
    //assert(count >= m_p);
    //assert((count / m_p) * m_p == count);

    // Compute peers
    m_peers = (uint**) malloc(sizeof(uint*)*m_p);
    int coord[MAX_SUPPORTED_DIMENSIONS];
    for(uint rank = 0; rank < m_p; rank++){
        m_peers[rank] = (uint*) malloc(sizeof(uint)*log2(m_p));
        
        int target_dim, relative_step;
        bool terminated_dimensions_bitmap[MAX_SUPPORTED_DIMENSIONS];
        for(size_t i = 0; i < MAX_SUPPORTED_DIMENSIONS; i++){
            terminated_dimensions_bitmap[i] = false;
        }
        uint terminated_dimensions = 0, o = 0;
        int next_rel_step[MAX_SUPPORTED_DIMENSIONS];
        for(size_t i = 0; i < MAX_SUPPORTED_DIMENSIONS; i++){
            next_rel_step[i] = 0;
        }
        int distance;
        
        // Generate peers
        for(size_t i = 0; i < log2(m_p); ){
            getCoordFromId(rank, coord); // Regenerate rank coord
            o = 0;
            do{
                target_dim = (m_port + i + o) % (m_dimensions_num);            
                o++;
            }while(terminated_dimensions_bitmap[target_dim]);
            //relative_step = (i + terminated_dimensions) / m_dimensions_num;        
            relative_step = next_rel_step[target_dim];
            next_rel_step[target_dim] = next_rel_step[target_dim] + 1;       


            //                                          Get only the first bits
            uint32_t mask = (0x1 << relative_step) & (m_dimensions[target_dim] - 1);
            distance = coord[target_dim] - (coord[target_dim] ^ mask);
            // Flip direction if needed
            if(m_port >= m_dimensions_num){
                distance *= -1;
            }

            if(relative_step >= log2(m_dimensions[target_dim])){
                terminated_dimensions_bitmap[target_dim] = true;
                terminated_dimensions++;
            }else{
                coord[target_dim] = mod((coord[target_dim] + distance), m_dimensions[target_dim]); // We need to use mod to avoid negative coordinates
                m_peers[rank][i] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);
                i += 1;
            }        
        }        
    }
}

void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::setEnable(bool enable)
{
    m_enabled = enable;
}

bool EmberRecDoubCollGenerator::RecDoubCollectiveEngine::isEnabled()
{
    return m_enabled;
}

bool EmberRecDoubCollGenerator::RecDoubCollectiveEngine::progress(std::queue<EmberEvent *> &evQ) 
{    
    switch (m_state)
    {
    case REDUCE_SCATTER: 
        if (!m_do_reduce_scatter || collective(evQ, RECDOUB_REDUCE_SCATTER)) 
        {
            m_state = ALL_GATHER;
        } else return false;
    case ALL_GATHER:
        if (!m_do_allgather || collective(evQ, RECDOUB_ALLGATHER))
        {
            m_state = FINI;
        } else return false;
    case FINI:
        return true;
    default: assert(0);
    }
    assert(0);
}

uint64_t EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getMovedBytes()
{
    return m_data_sent;
}

void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::setBuff(float *new_dest)
{
    m_dst = new_dest;
}

float* EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getBuff()
{
    return m_dst;
}

bool EmberRecDoubCollGenerator::RecDoubCollectiveEngine::hasPendingRecv()
{   
    return m_waiting_recv && m_ready_to_recv;
}

MessageRequest EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getRecvHandle()
{
    return m_req_recv;
}

void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::reset()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
    m_i = 0;
    m_state = REDUCE_SCATTER;
    m_tag1 = m_gen.getNextTag();
    m_tag2 = m_gen.getNextTag();
}

void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::notifyRecv()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
}

void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::processReceivedData(std::queue<EmberEvent *> &evQ, CollType coll_type)
{
    if(m_validate){
        size_t k = 0;
        for(size_t i = 0; i < m_p; i++){
            if(m_blocks_bitmap_r[i]){
                uint32_t recv_block_offset = getBlockOffset(i);
                uint32_t recv_block_size = getBlockSize(i);
                for(size_t j = recv_block_offset; j < recv_block_offset + recv_block_size; j++){
                    if(coll_type == RECDOUB_REDUCE_SCATTER){
                        m_dst[j] += m_tmp[k];
                    }else{
                        m_dst[j] = m_tmp[k];
                    }
                    ++k;
                }
                if(recv_block_size == m_count){ // To manage the case where msg is too small
                    break;
                }
            }                                                
        }
    }else{
        if(coll_type != RECDOUB_ALLGATHER){
            m_gen.enQ_compute(evQ, m_aggregation_cost_ns * m_recv_size);
        }
    }
}

uint32_t EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getBlockOffset(uint32_t block_idx)
{
    if (m_count < m_p) return 0;

    uint32_t block_size = m_count / m_p;
    return block_idx * block_size;
}

uint32_t EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getBlockSize(uint32_t block_idx)
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

// Convert a rank id into a list of d-dimensional coordinates (adapted from MPICH code -- https://github.com/pmodels/mpich/blob/94b1cd6f060cafbf68d6d83ea551a8bcc8fcecd4/src/mpi/topo/topo_impl.c)
void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getCoordFromId(int id, int* coord){
    int nnodes = m_p;
    for (int i = 0; i < m_dimensions_num; i++) {
        nnodes = nnodes / m_dimensions[i];
        coord[i] = id / nnodes;
        id = id % nnodes;
    }
    /*
    if(m_dimensions_num == 1){
        coord[0] = id;
    }else if(m_dimensions_num == 2){
        coord[0] = id / m_dimensions[1];
        coord[1] = id % m_dimensions[1];
    }else if(m_dimensions_num == 3){
        coord[0] = (id / m_dimensions[1]) % m_dimensions[0];
        coord[1] = id % m_dimensions[1];
        coord[2] = id / (m_dimensions[0]*m_dimensions[1]);
    }
    */
}

// Convert d-dimensional coordinates into a rank id (adapted from MPICH code -- https://github.com/pmodels/mpich/blob/94b1cd6f060cafbf68d6d83ea551a8bcc8fcecd4/src/mpi/topo/topo_impl.c)
int EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getIdFromCoord(int* coords, uint* dimensions, uint dimensions_num){
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
    /*
    if(dimensions_num == 1){
        return coord[0];
    }else if(dimensions_num == 2){
        return coord[0]*dimensions[1] + coord[1];
    }else if(dimensions_num == 3){    
        return int(coord[2]*(dimensions[0]*dimensions[1]) + getIdFromCoord(coord, dimensions, dimensions_num - 1));
    }else{
        return -1;
    }
    */
}

// Gets the peer of rank 'sender' at the step-th step on a dimension-dimensional network, considering the next_direction
int EmberRecDoubCollGenerator::RecDoubCollectiveEngine::getPeer(int sender, int step, CollType collective){
    //std::cout << "Sender " << sender << " Port " << m_port << " Peers: " << m_peers[sender][0] << " " << m_peers[sender][1] << " " << m_peers[sender][2] << " " << m_peers[sender][3] << " " << m_peers[sender][4] << " " << m_peers[sender][5] << std::endl;
    size_t index;
    if(collective == RECDOUB_REDUCE_SCATTER){
        index = step;
    }else{
        index = log2(m_p) - step - 1;
    }
    uint id = m_peers[sender][index];
    //std::cout << "port " << m_port << " " << sender << " talking to " << id << " at step " << step << std::endl;
    return id;
}

// Returns a list of indexes of the data to be sent in a reduce-scatter collective
// by 'sender' at step 'step' on a torus with 'dimensions' dimensions.
// Sets the position in 'blocks' to 1 if that block must be sent.
void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::computeReducescatterBlocksBitmap(int sender, int step, uint8_t* blocks_bitmap){
    if (step >= log2(m_p)){ // Base case
        return;
    }else{
        for(size_t s = step; s < int(log2(m_p)); s++){
            int peer = getPeer(sender, s, RECDOUB_REDUCE_SCATTER); 
            blocks_bitmap[peer] = 1;
            computeReducescatterBlocksBitmap(peer, s+1, blocks_bitmap);
        }
        return;
    }
}

// Returns a list of indexes of the data to be sent in an allgather collective
// by 'sender' at step 'step' on a torus with 'dimensions' dimensions.
// Sets the position in 'blocks' to 1 if that block must be sent.
void EmberRecDoubCollGenerator::RecDoubCollectiveEngine::computeAllgatherBlocksBitmap(int sender, int step, uint8_t* blocks_bitmap){
    if(step == 0){ // Base case
        blocks_bitmap[sender] = 1;
        return;
    }else{
        // I send whatever I sent in the previous step ...
        computeAllgatherBlocksBitmap(sender, step - 1, blocks_bitmap);
        // ... plus what I received in the previous step
        int peer = getPeer(sender, step - 1, RECDOUB_ALLGATHER);
        computeAllgatherBlocksBitmap(peer, step - 1, blocks_bitmap);
    }
}

bool EmberRecDoubCollGenerator::RecDoubCollectiveEngine::collective(std::queue<EmberEvent *> &evQ, CollType coll_type) 
{
    assert(coll_type == RECDOUB_REDUCE_SCATTER || coll_type == RECDOUB_ALLGATHER);
    uint32_t tag;
    if(coll_type == RECDOUB_REDUCE_SCATTER){
        tag = m_tag1;
    }else{
        tag = m_tag2;
    }


    DPRINTF("[%d] Starting step %d\n", m_r, m_i);
    // If we only have one rank, we do not need to do anything
    if (m_p <= 1){
        return true;
    }

    if (m_i > 0 && m_waiting_recv) 
    {
        // we are ready to recv the cycle after we post the recv. In the same cycle, when we post the irecv, the handle is 
        // still not valid as it will be filled when the irecv is executed (so after we return control to emberengine). 
        m_ready_to_recv = true;
        DPRINTF("[%d] Step %d, still did not recv\n", m_r, m_i);
        return false;
    }

    // Not to be done on first iteration
    if (m_i > 0){
        /**********************************************************************/
        /* Aggregate and clean previous send (terminates previous iteration). */
        /**********************************************************************/
        // if we are here, it means that the prev recv has been matched. 
        // now we process the received data (e.g., aggragate it if doing 
        // reducescatter) & move to the next step.
        m_waiting_recv = false;
        processReceivedData(evQ, coll_type);
        
        // cleanup the prev send
        m_gen.enQ_wait(evQ, &m_req_send);
        DPRINTF("[%d] Send cleaned at time %" PRIu64 "\n", m_gen.rank(), m_gen.getCurrentSimTimeNano());
    }

    /********************************************************************/
    /* To be done on all iterations except the last, posting send/recv. */
    /********************************************************************/
    if (m_i < log2(m_p))
    {
        /**********************************************************/
        /* First things first, who should I receive/send from/to? */
        /**********************************************************/
        int peer = getPeer(m_r, m_i, coll_type);
        
        if(m_latency_optimal){
            // For latency optimal, I send/recv all the data
            for(size_t i = 0; i < m_p; i++){
                m_blocks_bitmap_s[i] = 1;
                m_blocks_bitmap_r[i] = 1;
            }
        }else{
            /*********************************************************************/
            /* Now find which blocks I will receive. These are the block I need, */
            /* plus all of those I'll send starting from next step.              */
            /*********************************************************************/
            for(size_t i = 0; i < m_p; i++){
                m_blocks_bitmap_r[i] = 0;
            }        
            if(coll_type == RECDOUB_REDUCE_SCATTER){
                m_blocks_bitmap_r[m_r] = 1; // I'll receive my block ...
                computeReducescatterBlocksBitmap(m_r, m_i + 1, m_blocks_bitmap_r); // ... plus those I'll send in the next steps.
            }else{
                computeAllgatherBlocksBitmap(peer, m_i, m_blocks_bitmap_r);
            }

            /********/
            /* Send */
            /********/
            // Find which blocks I must send.
            for(size_t i = 0; i < m_p; i++){
                m_blocks_bitmap_s[i] = 0;
            }        
            if(coll_type == RECDOUB_REDUCE_SCATTER){
                m_blocks_bitmap_s[peer] = 1; // I'll send my peer's block ...
                computeReducescatterBlocksBitmap(peer, m_i + 1, m_blocks_bitmap_s); // ... plus all those my peer will send from the next step.
            }else{
                computeAllgatherBlocksBitmap(m_r, m_i, m_blocks_bitmap_s);
            }

        }
#ifdef DEBUG
        DPRINTF("[%d] Blocks Bitmap (Recv) at step %d: ", m_r, m_i);
        for(size_t i=0; i < m_p; i++){
            DPRINTF("%d ", m_blocks_bitmap_r[i]);
        }
        DPRINTF("\n");

        DPRINTF("[%d] Blocks Bitmap (Send) at step %d: ", m_r, m_i);
        for(size_t i=0; i < m_p; i++){
            DPRINTF("%d ", m_blocks_bitmap_s[i]);
        }
        DPRINTF("\n");
#endif
        /********/
        /* Recv */
        /********/
        // Differently from the ring allreduce, here we send/recv noncontiguous data.
        // This means that, at least when we validate, we need to copy the data back 
        // and forth from a temporary buffer.        
        m_recv_size = 0;
        for(size_t i = 0; i < m_p; i++){
            if(m_blocks_bitmap_r[i]){
                uint32_t recv_block_size = getBlockSize(i);    
                m_recv_size += recv_block_size;
                if(m_recv_size == m_count){ // To manage the case where msg is too small
                    break;
                }
            }                                                            
        }
        DPRINTF("[%d] Receiving %d elements from %d with tag %d on port %d\n", m_gen.rank(), m_recv_size, peer, tag, m_port);
        // Posting irecv
        m_gen.enQ_irecv(evQ, &m_tmp[0], m_recv_size, FLOAT, peer, tag, m_comm, &m_req_recv);            
        m_waiting_recv = true;

        // Differently from the ring allreduce, here we send/recv noncontiguous data.
        // This means that, at least when we validate, we need to copy the data back and forth from a temporary buffer.
        m_send_size = 0;
        for(size_t i = 0; i < m_p; i++){
            if(m_blocks_bitmap_s[i]){                
                uint32_t send_buff_size = getBlockSize(i);
                if(m_validate){
                    uint32_t send_buff_offset = getBlockOffset(i);
                    assert(m_send_size + send_buff_size <= m_count); // Actually that could be m_count / 2
                    assert(send_buff_offset + send_buff_size <= m_count);
                    DPRINTF("[%d] Copying %d bytes from offset %d (pointer %p) to offset %d (pointer %p)\n", 
                            m_r, send_buff_size * m_gen.sizeofDataType(FLOAT), 
                            send_buff_offset * m_gen.sizeofDataType(FLOAT), m_dst + send_buff_offset,
                            m_send_size * m_gen.sizeofDataType(FLOAT), m_send_tmp + m_send_size);
                    memcpy(m_send_tmp + m_send_size, m_dst + send_buff_offset, send_buff_size * m_gen.sizeofDataType(FLOAT));
                }
                m_send_size += send_buff_size;
                // To manage latency-optimized version.
                if(m_send_size == m_count){ // To manage the case where msg is too small
                    break;
                }
            }                 
        }
        DPRINTF("[%d] Sending %d elements to %d with tag %d on port %d at time %" PRIu64 "\n", m_gen.rank(), m_send_size, peer, tag, m_port, m_gen.getCurrentSimTimeNano());
        // Send
        /*printf(
            "COMM step=%u from=%u to=%u bytes=%u\n",
            m_i,
            m_r,
            peer,
            m_send_size * m_gen.sizeofDataType(FLOAT)
        );
        fflush(stdout);*/
        if(m_validate){
            m_gen.enQ_isend(evQ, &m_send_tmp[0], m_send_size, FLOAT, peer, tag, m_comm, &m_req_send);
        }else{
            // I am not validating the content so I can send any garbage
            m_gen.enQ_isend(evQ, &m_dst[0], m_send_size, FLOAT, peer, tag, m_comm, &m_req_send);
        }
        m_data_sent += m_send_size * m_gen.sizeofDataType(FLOAT);
    }

    m_i++;
    if (m_i <= log2(m_p)) return false;
    else {
        m_i = 0;
        return true;
    }
}

/******* CollectiveBase  *******/
EmberRecDoubCollGenerator::RecDoubCollectiveRunner::RecDoubCollectiveRunner(EmberRecDoubCollGenerator &gen, int num_allreduces, uint32_t count, 
                                                                      uint32_t rank, uint32_t comm_size, bool nonblocking, bool sync)
 : m_gen(gen), m_nb(nonblocking), m_sync(sync), m_has_new_recv(false), m_count(count), m_r(rank), m_p(comm_size), m_latency_optimal(false)
{
    // tracking active and with-pending-receive allreduces
    m_active_recv_handles.resize(num_allreduces);
    m_active_allreduce_ptrs.resize(num_allreduces);

    // counter of posted recvs
    m_handle_idx = 0;

    // index of last matched recv in m_active_recv_handles
    m_recv_idx = 0;
}

bool EmberRecDoubCollGenerator::RecDoubCollectiveRunner::progress_phase(std::queue<EmberEvent *> &evQ)
{
    if(m_sync){
        m_handle_idx = 0;
        bool all_completed = true;
        for (auto &allreduce : m_allreduces)
        {
            // we don't progress disabled allreduces (they look as completed)
            bool this_completed = !allreduce.isEnabled() || allreduce.progress(evQ);
            all_completed = all_completed && this_completed;
            if (allreduce.hasPendingRecv())
            {
                DPRINTF("[%d] Found one pending recv\n", m_r);
                assert(!this_completed);
                // we need to do this because SST doesn't like invalid recv handles in the waitany :(
                //printf("[%d] allreduce %p waiting for %p\n", m_gen.rank(), &allreduce, allreduce.getRecvHandle());
                m_active_recv_handles[m_handle_idx] = allreduce.getRecvHandle();
                m_active_allreduce_ptrs[m_handle_idx] = &allreduce;
                m_handle_idx++;
            }else{
                DPRINTF("[%d] Found one non-pending recv\n", m_r);
            }
        }
        if(m_handle_idx){
            DPRINTF("[%d] Waiting for %d recvs\n", m_r, m_handle_idx);
            m_gen.enQ_waitall(evQ, m_handle_idx, &m_active_recv_handles[0], NULL);
            m_gen.enQ_compute(evQ, [&]() {
                for(auto& allreduce: m_active_allreduce_ptrs){
                    DPRINTF("[%d] Notifying\n", m_r);
                    allreduce->notifyRecv();
                }    
                return 0;
            });
        }
        return all_completed;
    }else{
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
            bool this_completed = false;
            if(allreduce.isEnabled()){
                this_completed = allreduce.progress(evQ);
            }else{
                this_completed = true;
            }
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
}

void EmberRecDoubCollGenerator::RecDoubCollectiveRunner::printStats() 
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

/******* RecDoubCollective  *******/
EmberRecDoubCollGenerator::RecDoubCollective::RecDoubCollective(EmberRecDoubCollGenerator &gen, uint dimensions_num, uint ports, uint32_t count, 
                                                                uint32_t rank, uint32_t comm_size, Communicator comm, 
                                                                double aggregation_cost_ns, bool nb, bool sync, 
                                                                CollType coll_type, float* data, uint* dimensions, bool latency_optimal)
: RecDoubCollectiveRunner(gen, ports, count, rank, comm_size, nb, sync), m_state(INIT), m_coll_type(coll_type)
{
    this->m_latency_optimal = latency_optimal;
    if(dimensions == NULL){
        dimensions = (uint*) malloc(sizeof(uint)*MAX_SUPPORTED_DIMENSIONS);
        if(dimensions_num == 1){
            dimensions[0] = m_p;
            DPRINTF("Considering a logical topology of size %d\n", dimensions[0]);
        }else if(dimensions_num == 2){
            dimensions[0] = sqrt(m_p);
            dimensions[1] = sqrt(m_p);
            DPRINTF("Considering a logical topology of size %dx%d\n", dimensions[0], dimensions[1]);
        }else if(dimensions_num == 3){
            dimensions[0] = cbrt(m_p);
            dimensions[1] = cbrt(m_p);
            dimensions[2] = cbrt(m_p);
            DPRINTF("Considering a logical topology of size %dx%dx%d\n", dimensions[0], dimensions[1], dimensions[2]);
        }else{
            fprintf(stderr, "%d dimensions not supported.", dimensions_num);
            exit(-1);
        }
    }

    // Check that each dimension is a power of 2
    for(size_t i = 0; i < dimensions_num; i++){
        double l = log2(dimensions[i]);
        assert(floor(l) == ceil(l));
    }
    
    int max_ports = std::min(count, ports);
    if(m_r == 0){
        DPRINTF("[%d] Count is %d, thus we will use %d ports\n", m_r, m_count, max_ports);
    }
            
    for(size_t i = 0; i < max_ports; i++){        
        uint chunk_size,start_offset;
        if (count >= m_p) {
            chunk_size = m_count / max_ports;
            start_offset = i*chunk_size;
            if(i == max_ports - 1){
                chunk_size = m_count - (chunk_size)*(max_ports-1);
            }
        } else {
            chunk_size = m_count;
            start_offset = 0;
        }  
        DPRINTF("[%d] Allreduce on port %d will go from %d to %d\n", m_r, i, start_offset, start_offset + chunk_size);
        m_allreduces.push_back(RecDoubCollectiveEngine(m_gen, coll_type, dimensions, dimensions_num, data + start_offset, chunk_size, 
                                                     m_r, m_p, aggregation_cost_ns, comm, data != NULL, i, latency_optimal));
    }
}

bool EmberRecDoubCollGenerator::RecDoubCollective::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT:
        m_gen.enQ_getTime(evQ, &m_start_time);
        m_gen.enQ_compute(evQ, m_count >= m_p && !m_latency_optimal ? 2*ceil(log2(m_p))*1500 : ceil(log2(m_p))*1500);  // 1000 ns
        m_state = PHASE_1;
        // no break/return needed here
    case PHASE_1: 
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
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

void EmberRecDoubCollGenerator::RecDoubCollective::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberRecDoubCollGenerator::RecDoubCollective::~RecDoubCollective()
{
    ;
}

/******* EmberHxMeshGenerator (parent Ember motif) *******/
EmberRecDoubCollGenerator::EmberRecDoubCollGenerator(SST::ComponentId_t id, Params &params)
: EmberMessagePassingGenerator(id, params, "None"), m_tag(UINT_MAX)
{
    assert(0);
} 

EmberRecDoubCollGenerator::EmberRecDoubCollGenerator(SST::ComponentId_t id, Params &params, std::string name) 
: EmberMessagePassingGenerator(id, params, name), m_tag(UINT_MAX)
{
    ;
}

bool EmberRecDoubCollGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    assert(0);
}
