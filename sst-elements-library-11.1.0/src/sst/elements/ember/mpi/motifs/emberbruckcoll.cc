#include <sst_config.h>
#include <sst/core/rng/xorshift.h>
#include <limits.h>
#include "emberbruckcoll.h"
#include <cmath>
#include <functional>

//#define DEBUG

#define NO_PEER -1
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

static inline uint ceil_log3_u64(uint64_t n) {
    if (n <= 1) return 0;          
    uint k = 0;
    uint64_t p = 1;
    while (p < n) {                
        if (p > UINT64_MAX / 3) {  
            return k + 1;
        }
        p *= 3;
        ++k;
    }
    return k;
}

static inline uint64_t pow3_u64(uint k) {
    uint64_t p = 1;
    while (k--) p *= 3;
    return p;
}

static inline uint get_num_steps(uint* dimensions, uint dimensions_num) {
    uint steps = 0;
    for (uint d = 0; d < dimensions_num; d++) {
        steps += ceil_log3_u64(dimensions[d]);
    }
    return steps;
}

EmberBruckCollGenerator::BruckCollectiveEngine::BruckCollectiveEngine(EmberBruckCollGenerator &gen, CollType coll_type, uint* dimensions, uint dimensions_num,
                                                                      float *dst, uint32_t count, uint32_t vrank, uint32_t numproc, double aggregation_cost_ns, 
                                                                      Communicator comm, bool validate, uint port, bool latency_optimal)
    :  m_gen(gen), m_count(count), m_dst(dst), m_r(vrank), m_p(numproc), m_aggregation_cost_ns(aggregation_cost_ns), m_data_sent(0), 
       m_comm(comm), m_dimensions(dimensions), m_dimensions_num(dimensions_num), m_validate(validate), m_port(port), m_enabled(true),
       m_latency_optimal(latency_optimal)
{

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
    case BRUCK_ALLREDUCE:
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
    case BRUCK_REDUCE_SCATTER:
        m_do_reduce_scatter = true;
        m_do_allgather = false;
        break;
    case BRUCK_ALLGATHER:
        m_do_reduce_scatter = false;
        m_do_allgather = true;
        break;
    default: assert(0);
    }

    reset();

    const uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);
    m_peers = (uint**) malloc(sizeof(uint*)*m_p);
    m_peers_o = (uint**) malloc(sizeof(uint*)*m_p);
    m_peers_a = (uint**) malloc(sizeof(uint*)*m_p);
    int coord[MAX_SUPPORTED_DIMENSIONS];
    bool terminated_dimensions_bitmap[MAX_SUPPORTED_DIMENSIONS];
    int next_directions[MAX_SUPPORTED_DIMENSIONS];
    for(uint rank = 0; rank < m_p; rank++){
        m_peers[rank]   = (uint*) malloc(sizeof(uint)*num_steps);
        m_peers_o[rank] = (uint*) malloc(sizeof(uint)*num_steps);
        m_peers_a[rank] = (uint*) malloc(sizeof(uint)*num_steps);


        getCoordFromId(rank, coord);
        for(size_t i = 0; i < m_dimensions_num; i++){
            next_directions[i] = std::pow(-1, ((coord[i] % 2) + (m_port % 2)));
            terminated_dimensions_bitmap[i] = false;            
        }
        
        int target_dim, relative_step, distance;
        uint terminated_dimensions = 0, o = 0;
        int next_rel_step[MAX_SUPPORTED_DIMENSIONS];
        for(size_t i = 0; i < MAX_SUPPORTED_DIMENSIONS; i++){
            next_rel_step[i] = 0;
        }
        
        for(size_t i = 0; i < num_steps; ){
            getCoordFromId(rank, coord); // Regenerate rank coord
            o = 0;
            do{
                target_dim = (m_port + i + o) % (m_dimensions_num);            
                o++;
            }while(terminated_dimensions_bitmap[target_dim]);
            relative_step = next_rel_step[target_dim];
            next_rel_step[target_dim] = next_rel_step[target_dim] + 1;
            if (relative_step >= ceil_log3_u64(m_dimensions[target_dim])-1) {
                distance = mod(pow(3, relative_step),m_dimensions[target_dim]); 
            } else {
                distance = mod(pow(3, relative_step),m_dimensions[target_dim]); 

            }
            if(m_port >= m_dimensions_num){ // Mirrored collectives
                distance = 2*mod(pow(3, relative_step),m_dimensions[target_dim]); 
            }
            if(relative_step >= ceil_log3_u64(m_dimensions[target_dim])){
                terminated_dimensions_bitmap[target_dim] = true;
                terminated_dimensions++;
            }else{
                int base = coord[target_dim];
                int c_minus;
                int c_plus;
                int c_a;
                
                if (m_port >= m_dimensions_num){
                    c_a = mod(base + distance/2, m_dimensions[target_dim]);
                } else {
                    c_a = mod(base + distance*2, m_dimensions[target_dim]);
                }
                

                c_minus = mod(base - distance, m_dimensions[target_dim]);
                c_plus  = mod(base + distance, m_dimensions[target_dim]);

                coord[target_dim] = c_a;
                m_peers_a[rank][i] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);

                coord[target_dim] = c_minus;
                m_peers_o[rank][i] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);

                coord[target_dim] = c_plus;
                m_peers[rank][i] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);

                i += 1;
                next_directions[target_dim] *= -1;
            }        
        }        
    }
    m_my_blocks_matrix = getBitmaps(m_r);
}

uint8_t** EmberBruckCollGenerator::BruckCollectiveEngine::getBitmaps(int rank){
    const uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);
    const uint D = m_dimensions_num;

    uint low_port, high_port;
    if (m_port < D) {
        low_port = m_port;
        high_port = m_port + D;
    } else {
        low_port = m_port - D;
        high_port = m_port;
    }
    const bool return_high = (m_port >= D);

    auto alloc_peer_tables = [&](uint logical_port, uint*** peers_out, uint*** peers_a_out) {
        uint** peers = (uint**) malloc(sizeof(uint*) * m_p);
        uint** peers_a = (uint**) malloc(sizeof(uint*) * m_p);
        int coord[MAX_SUPPORTED_DIMENSIONS];
        bool terminated_dimensions_bitmap[MAX_SUPPORTED_DIMENSIONS];
        int next_directions[MAX_SUPPORTED_DIMENSIONS];

        for (uint r = 0; r < m_p; r++) {
            peers[r] = (uint*) malloc(sizeof(uint) * num_steps);
            peers_a[r] = (uint*) malloc(sizeof(uint) * num_steps);

            getCoordFromId(r, coord);
            for (size_t d = 0; d < m_dimensions_num; d++) {
                next_directions[d] = std::pow(-1, ((coord[d] % 2) + (logical_port % 2)));
                terminated_dimensions_bitmap[d] = false;
            }

            int next_rel_step[MAX_SUPPORTED_DIMENSIONS];
            for (size_t d = 0; d < MAX_SUPPORTED_DIMENSIONS; d++) {
                next_rel_step[d] = 0;
            }

            for (size_t step = 0; step < num_steps; ) {
                getCoordFromId(r, coord);

                uint o = 0;
                int target_dim;
                do {
                    target_dim = (logical_port + step + o) % m_dimensions_num;
                    o++;
                } while (terminated_dimensions_bitmap[target_dim]);

                int relative_step = next_rel_step[target_dim];
                next_rel_step[target_dim]++;

                int distance;
                if (relative_step >= (int)ceil_log3_u64(m_dimensions[target_dim]) - 1) {
                    distance = mod(pow3_u64(relative_step), m_dimensions[target_dim]);
                } else {
                    distance = mod(pow3_u64(relative_step), m_dimensions[target_dim]);
                }

                if (logical_port >= m_dimensions_num) {
                    distance = 2 * mod(pow3_u64(relative_step), m_dimensions[target_dim]);
                }

                if (relative_step >= (int)ceil_log3_u64(m_dimensions[target_dim])) {
                    terminated_dimensions_bitmap[target_dim] = true;
                    continue;
                }

                int base = coord[target_dim];
                int c_a;
                if (logical_port >= m_dimensions_num) {
                    c_a = mod(base + distance / 2, m_dimensions[target_dim]);
                } else {
                    c_a = mod(base + distance * 2, m_dimensions[target_dim]);
                }

                int c_plus = mod(base + distance, m_dimensions[target_dim]);

                coord[target_dim] = c_a;
                peers_a[r][step] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);

                coord[target_dim] = c_plus;
                peers[r][step] = getIdFromCoord(coord, m_dimensions, m_dimensions_num);

                step++;
                next_directions[target_dim] *= -1;
            }
        }

        *peers_out = peers;
        *peers_a_out = peers_a;
    };

    auto free_peer_tables = [&](uint** peers, uint** peers_a) {
        for (uint r = 0; r < m_p; r++) {
            free(peers[r]);
            free(peers_a[r]);
        }
        free(peers);
        free(peers_a);
    };

    std::function<void(int,int,uint8_t*,uint**,uint**)> compute_raw =
        [&](int sender, int step, uint8_t* blocks_bitmap, uint** peers, uint** peers_a) {
            if (step >= (int)num_steps) {
                return;
            }
            for (size_t s = step; s < num_steps; s++) {
                int peer1 = peers[sender][s];
                blocks_bitmap[peer1] = 1;
                compute_raw(peer1, s + 1, blocks_bitmap, peers, peers_a);

                int peer2 = peers_a[sender][s];
                blocks_bitmap[peer2] = 1;
                compute_raw(peer2, s + 1, blocks_bitmap, peers, peers_a);
            }
        };

    auto raw_bitmaps_for = [&](uint** peers, uint** peers_a) {
        uint8_t** bitmaps = (uint8_t**) malloc(sizeof(uint8_t*) * num_steps);
        uint8_t* reached_step = (uint8_t*) malloc(sizeof(uint8_t) * m_p);
        memset(reached_step, num_steps, sizeof(uint8_t) * m_p);

        for (size_t step = 0; step < num_steps; step++) {
            bitmaps[step] = (uint8_t*) malloc(sizeof(uint8_t) * m_p);
            memset(bitmaps[step], 0, sizeof(uint8_t) * m_p);

            int dest = peers[rank][step];
            bitmaps[step][dest] = 1;
            compute_raw(dest, step + 1, bitmaps[step], peers, peers_a);

            bitmaps[step][rank] = 0;
            for (size_t b = 0; b < m_p; b++) {
                if (bitmaps[step][b]) {
                    if (reached_step[b] != num_steps) {
                        int prev_step = reached_step[b];
                        bitmaps[prev_step][b] = 0;
                    }
                    reached_step[b] = step;
                }
            }
        }

        free(reached_step);
        return bitmaps;
    };

    auto free_bitmaps = [&](uint8_t** bitmaps) {
        for (size_t s = 0; s < num_steps; s++) {
            free(bitmaps[s]);
        }
        free(bitmaps);
    };

    uint** low_peers;
    uint** low_peers_a;
    uint** high_peers;
    uint** high_peers_a;

    alloc_peer_tables(low_port, &low_peers, &low_peers_a);
    alloc_peer_tables(high_port, &high_peers, &high_peers_a);

    uint8_t** low_raw = raw_bitmaps_for(low_peers, low_peers_a);
    uint8_t** high_raw = raw_bitmaps_for(high_peers, high_peers_a);

    uint8_t** result = (uint8_t**) malloc(sizeof(uint8_t*) * num_steps);
    for (size_t s = 0; s < num_steps; s++) {
        result[s] = (uint8_t*) malloc(sizeof(uint8_t) * m_p);
        memset(result[s], 0, sizeof(uint8_t) * m_p);
    }

    uint64_t* low_load = (uint64_t*) calloc(num_steps, sizeof(uint64_t));
    uint64_t* high_load = (uint64_t*) calloc(num_steps, sizeof(uint64_t));

    for (size_t block = 0; block < m_p; block++) {
        if ((int)block == rank) {
            continue;
        }

        bool found = false;
        bool choose_high = false;
        uint best_step = 0;
        uint64_t best_load = UINT64_MAX;

        for (size_t s = 0; s < num_steps; s++) {
            if (low_raw[s][block]) {
                uint64_t load = low_load[s];
                if (!found || load < best_load) {
                    found = true;
                    choose_high = false;
                    best_step = s;
                    best_load = load;
                }
            }
            if (high_raw[s][block]) {
                uint64_t load = high_load[s];
                if (!found || load < best_load) {
                    found = true;
                    choose_high = true;
                    best_step = s;
                    best_load = load;
                }
            }
        }

        if (!found) {
            continue;
        }

        uint64_t block_bytes = (uint64_t)getBlockSize(block) * m_gen.sizeofDataType(FLOAT);
        if (choose_high) {
            high_load[best_step] += block_bytes;
            if (return_high) {
                result[best_step][block] = 1;
            }
        } else {
            low_load[best_step] += block_bytes;
            if (!return_high) {
                result[best_step][block] = 1;
            }
        }
    }

    free(low_load);
    free(high_load);

    free_bitmaps(low_raw);
    free_bitmaps(high_raw);
    free_peer_tables(low_peers, low_peers_a);
    free_peer_tables(high_peers, high_peers_a);

    return result;
}

void EmberBruckCollGenerator::BruckCollectiveEngine::setEnable(bool enable)
{
    m_enabled = enable;
}

bool EmberBruckCollGenerator::BruckCollectiveEngine::isEnabled()
{
    return m_enabled;
}

bool EmberBruckCollGenerator::BruckCollectiveEngine::progress(std::queue<EmberEvent *> &evQ) 
{    
    switch (m_state)
    {
    case REDUCE_SCATTER: 
        if (!m_do_reduce_scatter || collective(evQ, BRUCK_REDUCE_SCATTER)) 
        {
            m_state = ALL_GATHER;
        } else return false;
    case ALL_GATHER:
        if (!m_do_allgather || collective(evQ, BRUCK_ALLGATHER))
        {
            m_state = FINI;
        } else return false;
    case FINI:
        return true;
    default: assert(0);
    }
    assert(0);
}

uint64_t EmberBruckCollGenerator::BruckCollectiveEngine::getMovedBytes()
{
    return m_data_sent;
}

void EmberBruckCollGenerator::BruckCollectiveEngine::setBuff(float *new_dest)
{
    m_dst = new_dest;
}

float* EmberBruckCollGenerator::BruckCollectiveEngine::getBuff()
{
    return m_dst;
}

bool EmberBruckCollGenerator::BruckCollectiveEngine::hasPendingRecv()
{   
    return m_waiting_recv && m_ready_to_recv;
}

MessageRequest EmberBruckCollGenerator::BruckCollectiveEngine::getRecvHandle()
{
    return m_req_recv;
}

void EmberBruckCollGenerator::BruckCollectiveEngine::reset()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
    m_i = 0;
    m_state = REDUCE_SCATTER;
    m_tag1 = m_gen.getNextTag();
    m_tag2 = m_gen.getNextTag();
}

void EmberBruckCollGenerator::BruckCollectiveEngine::notifyRecv()
{
    m_waiting_recv = false;
    m_ready_to_recv = false;
    m_req_recv = 0;
}

void EmberBruckCollGenerator::BruckCollectiveEngine::processReceivedData(std::queue<EmberEvent *> &evQ, CollType coll_type)
{
    if(m_validate){
        size_t k = 0;
        for(size_t i = 0; i < m_p; i++){
            if(m_blocks_bitmap_r[i]){
                uint32_t recv_block_offset = getBlockOffset(i);
                uint32_t recv_block_size = getBlockSize(i);
                for(size_t j = recv_block_offset; j < recv_block_offset + recv_block_size; j++){
                    if(coll_type == BRUCK_REDUCE_SCATTER){
                        DPRINTF("[%d] Idx %d Summing %f to %f (total %f)\n", m_r, j, m_tmp[k], m_dst[j], m_tmp[k]+m_dst[j]);
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
        if(coll_type != BRUCK_ALLGATHER && m_aggregation_cost_ns != 0){
            m_gen.enQ_compute(evQ, m_aggregation_cost_ns * m_recv_size);
        }
    }
}

uint32_t EmberBruckCollGenerator::BruckCollectiveEngine::getBlockOffset(uint32_t block_idx)
{
    if (m_count < m_p) return 0;

    uint32_t block_size = m_count / m_p;
    return block_idx * block_size;
}

uint32_t EmberBruckCollGenerator::BruckCollectiveEngine::getBlockSize(uint32_t block_idx)
{
    if (m_count < m_p) return m_count;

    uint32_t block_size = m_count / m_p;
    uint32_t num_blocks = m_p;
    assert(block_idx >= 0 && block_idx < num_blocks);
    if (block_idx < num_blocks - 1)  {
        return block_size;
    } 
    else {
        return m_count - (block_size * (num_blocks - 1));
    } 
}

void EmberBruckCollGenerator::BruckCollectiveEngine::getCoordFromId(int id, int* coord){
    int nnodes = m_p;
    for (int i = 0; i < m_dimensions_num; i++) {
        nnodes = nnodes / m_dimensions[i];
        coord[i] = id / nnodes;
        id = id % nnodes;
    }

}

int EmberBruckCollGenerator::BruckCollectiveEngine::getIdFromCoord(int* coords, uint* dimensions, uint dimensions_num){
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

int EmberBruckCollGenerator::BruckCollectiveEngine::getPeer(int sender, int step, CollType collective){
    //std::cout << "Sender " << sender << " Port " << m_port << " Peers: " << m_peers[sender][0] << " " << m_peers[sender][1] << " " << m_peers[sender][2] << " " << m_peers[sender][3] << " " << m_peers[sender][4] << " " << m_peers[sender][5] << std::endl;

    size_t index;
    uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);

    if(collective == BRUCK_REDUCE_SCATTER){
        index = step;
    }else{
        index = num_steps - step - 1;
    }
    uint32_t id = m_peers[sender][index];
    DPRINTF("Sender %d Port %d Step %d peer %d coll %d\n", sender, m_port, step, id, collective);
    return id;
}

int EmberBruckCollGenerator::BruckCollectiveEngine::getPeer_o(int sender, int step, CollType collective){
    //std::cout << "Sender " << sender << " Port " << m_port << " Peers: " << m_peers[sender][0] << " " << m_peers[sender][1] << " " << m_peers[sender][2] << " " << m_peers[sender][3] << " " << m_peers[sender][4] << " " << m_peers[sender][5] << std::endl;

    size_t index;
    uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);

    if(collective == BRUCK_REDUCE_SCATTER){
        index = step;
    }else{
        index = num_steps - step - 1;
    }
    uint32_t id = m_peers_o[sender][index];
    DPRINTF("Sender %d Port %d Step %d peer %d coll %d\n", sender, m_port, step, id, collective);
    return id;
}

void EmberBruckCollGenerator::BruckCollectiveEngine::computeBlocksBitmap(int sender, int step, uint8_t* blocks_bitmap){
    uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);

    if (step >= num_steps){
        return;
    }else{
        for(size_t s = step; s < num_steps; s++){
            int peer1 = m_peers[sender][s];
            blocks_bitmap[peer1] = 1;
            computeBlocksBitmap(peer1, s+1, blocks_bitmap);

            int peer2 = m_peers_a[sender][s];
            blocks_bitmap[peer2] = 1;
            computeBlocksBitmap(peer2, s+1, blocks_bitmap);
        }
        return;
    }    
}

bool EmberBruckCollGenerator::BruckCollectiveEngine::collective(std::queue<EmberEvent *> &evQ, CollType coll_type) 
{
    assert(coll_type == BRUCK_REDUCE_SCATTER || coll_type == BRUCK_ALLGATHER);
    uint32_t tag;
    uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);
    if(coll_type == BRUCK_REDUCE_SCATTER){
        tag = m_tag1;
    }else{
        tag = m_tag2;
    }


    DPRINTF("[%d] Starting step %d\n", m_r, m_i);
    if (m_p <= 1){
        return true;
    }

    if (m_i > 0 && m_waiting_recv) 
    {
        m_ready_to_recv = true;
        DPRINTF("[%d] Step %d, still did not recv\n", m_r, m_i);
        return false;
    }

    if (m_i > 0){
        m_waiting_recv = false;
        processReceivedData(evQ, coll_type);
        
        m_gen.enQ_wait(evQ, &m_req_send);
        DPRINTF("[%d] Send cleaned at time %" PRIu64 "\n", m_gen.rank(), m_gen.getCurrentSimTimeNano());
    }

    if (m_i < num_steps)
    {
                
        int peer = getPeer(m_r, m_i, coll_type);
        int peer_o = getPeer_o(m_r, m_i, coll_type);
        
        if(m_latency_optimal){
            for(size_t i = 0; i < m_p; i++){
                m_blocks_bitmap_s[i] = 1;
                m_blocks_bitmap_r[i] = 1;
            }
        }else{
            DPRINTF("[%d] Computing adjusted blocks\n", m_r);
            uint8_t** peer_blocks_matrix = getBitmaps(peer_o);
            if(coll_type == BRUCK_REDUCE_SCATTER){
                for(size_t i = 0; i < m_p; i++){
                    m_blocks_bitmap_s[i] = m_my_blocks_matrix[m_i][i];
                    m_blocks_bitmap_r[i] = peer_blocks_matrix[m_i][i];
                }
            }else{
                for(size_t i = 0; i < m_p; i++){
                    uint reversed_step = int(num_steps - m_i - 1);
                    m_blocks_bitmap_s[i] = m_my_blocks_matrix[reversed_step][i];
                    m_blocks_bitmap_r[i] = peer_blocks_matrix[reversed_step][i];
                }
            }
            for(size_t i = 0; i < num_steps; i++){
                free(peer_blocks_matrix[i]);
            }
            free(peer_blocks_matrix);
        }
#ifdef DEBUG
        DPRINTF("[%d] Blocks Bitmap (Send) at step %d: ", m_r, m_i);
        for(size_t i=0; i < m_p; i++){
            DPRINTF("%d ", m_blocks_bitmap_s[i]);
        }
        DPRINTF("\n");

        DPRINTF("[%d] Blocks Bitmap (Recv) at step %d: ", m_r, m_i);
        for(size_t i=0; i < m_p; i++){
            DPRINTF("%d ", m_blocks_bitmap_r[i]);
        }
        DPRINTF("\n");
#endif

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
        DPRINTF("[%d] Receiving %d elements from %d with tag %d on port %d\n", m_gen.rank(), m_recv_size, peer_o, tag, m_port);

        float* recv_buffer = m_validate ? &m_tmp[0] : (m_dst != NULL ? &m_dst[0] : NULL);
        m_gen.enQ_irecv(evQ, recv_buffer, m_recv_size, FLOAT, peer_o, tag, m_comm, &m_req_recv);  
       
        m_waiting_recv = true;
        m_ready_to_recv = false;

        m_gen.enQ_compute(evQ, [&]() {
            m_ready_to_recv = true;
            return 0;
        });

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
                if(m_send_size == m_count){ // To manage the case where msg is too small
                    break;
                }
            }                 
        }
        DPRINTF("[%d] Sending %d elements to %d with tag %d on port %d at time %" PRIu64 "\n", m_gen.rank(), m_send_size, peer, tag, m_port, m_gen.getCurrentSimTimeNano());

        printf(
            "COMM step=%u port=%u from=%u to=%u bytes=%u\n",
            m_i,
            m_port,
            m_r,
            peer,
            m_send_size * m_gen.sizeofDataType(FLOAT)
        );
        fflush(stdout);
        if(m_validate){
            m_gen.enQ_isend(evQ, &m_send_tmp[0], m_send_size, FLOAT, peer, tag, m_comm, &m_req_send);
        }else{
            float* send_buffer = (m_dst != NULL) ? &m_dst[0] : NULL;
            m_gen.enQ_isend(evQ, send_buffer, m_send_size, FLOAT, peer, tag, m_comm, &m_req_send);
        }
        m_data_sent += m_send_size * m_gen.sizeofDataType(FLOAT);
    }
    m_i++;
    
    if (m_waiting_recv) return false;

    if (m_i <= num_steps) return false;

    m_i = 0;
    return true;
}

/******* CollectiveBase  *******/
EmberBruckCollGenerator::BruckCollectiveRunner::BruckCollectiveRunner(EmberBruckCollGenerator &gen, int num_allreduces, uint32_t count, 
                                                                      uint32_t rank, uint32_t comm_size, bool nonblocking, bool sync)
 : m_gen(gen), m_nb(nonblocking), m_sync(sync), m_has_new_recv(false), m_count(count), m_r(rank), m_p(comm_size), m_latency_optimal(false), m_dimensions_num(0), m_dimensions(0)
{    
    m_active_recv_handles.resize(num_allreduces);
    m_active_allreduce_ptrs.resize(num_allreduces);

    m_handle_idx = 0;

    m_recv_idx = 0;
}

bool EmberBruckCollGenerator::BruckCollectiveRunner::progress_phase(std::queue<EmberEvent *> &evQ)
{
    if(m_sync){
        m_handle_idx = 0;
        bool all_completed = true;
        for (auto &allreduce : m_allreduces)
        {
            bool this_completed = !allreduce.isEnabled() || allreduce.progress(evQ);
            all_completed = all_completed && this_completed;
            if (allreduce.hasPendingRecv())
            {
                DPRINTF("[%d] Found one pending recv\n", m_r);
                assert(!this_completed);
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
        if (m_handle_idx > 0 && m_has_new_recv) 
        {
            m_active_allreduce_ptrs[m_recv_idx]->notifyRecv();
        }

        m_handle_idx = 0;
        m_has_new_recv = false;
        bool all_completed = true;
        for (auto &allreduce : m_allreduces)
        {
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
                m_active_recv_handles[m_handle_idx] = allreduce.getRecvHandle();
                m_active_allreduce_ptrs[m_handle_idx] = &allreduce;
                m_handle_idx++;
            }
        }
        
        if (m_handle_idx > 0)
        {
            if (m_nb) {
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

void EmberBruckCollGenerator::BruckCollectiveRunner::printStats() 
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

/******* BruckCollective  *******/
EmberBruckCollGenerator::BruckCollective::BruckCollective(EmberBruckCollGenerator &gen, uint dimensions_num, uint ports, uint32_t count, 
                                                                      uint32_t rank, uint32_t comm_size, Communicator comm, 
                                                                      double aggregation_cost_ns, bool nb, bool sync, 
                                                                      CollType coll_type, float* data, uint* dimensions, bool latency_optimal)
: BruckCollectiveRunner(gen, std::min(count, ports), count, rank, comm_size, nb, sync), m_state(INIT), m_coll_type(coll_type)

{
    this->m_latency_optimal = latency_optimal;
    this->m_dimensions = dimensions;
    this->m_dimensions_num = dimensions_num;
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

   int max_ports = std::min(count, ports);
   int pairs = max_ports / 2;                           
   if(m_r == 0){
    DPRINTF("[%d] Count is %d, thus we will use %d ports\n", m_r, m_count, max_ports);
   }
    
    for(size_t i = 0; i < max_ports; i++){        
        int pair_id = i % pairs;
        uint chunk_size,start_offset;

        if (count >= m_p) {
            chunk_size = m_count / pairs;
            start_offset = pair_id * chunk_size;

            if (pair_id == pairs - 1) {
                chunk_size = m_count - chunk_size * (pairs - 1);
            }
        } else {
            chunk_size = m_count;
            start_offset = 0;
        }
    
        DPRINTF("[%d] Allreduce on port %d will go from %d to %d\n", m_r, i, start_offset, start_offset + chunk_size);
        float* engine_data = (data != NULL) ? data + start_offset : NULL;
        m_allreduces.push_back(BruckCollectiveEngine(m_gen, coll_type, dimensions, dimensions_num, engine_data, chunk_size, 
                                                     m_r, m_p, aggregation_cost_ns, comm, engine_data != NULL, i, latency_optimal));
    }
}

bool EmberBruckCollGenerator::BruckCollective::progress(std::queue<EmberEvent *> &evQ)
{
    switch (m_state)
    {
    case INIT: {
        m_gen.enQ_getTime(evQ, &m_start_time);
        const uint phases = (m_count >= m_p && !m_latency_optimal) ? 2 : 1;
        const uint num_steps = get_num_steps(m_dimensions, m_dimensions_num);
        m_gen.enQ_compute(evQ, (uint64_t)phases * num_steps *1500);
        m_state = PHASE_1;
        }
    case PHASE_1: 
        if (!progress_phase(evQ)) return false;
        assert(m_handle_idx==0);
        m_gen.enQ_getTime(evQ, &m_stop_time);
        m_state = FINI;
        return false;
    case FINI:
        return true;
    default:
        assert(0);
    }
    assert(0);
}

void EmberBruckCollGenerator::BruckCollective::reset()
{
    m_state = INIT;
    for (auto &allreduce : m_allreduces) allreduce.reset();
}

EmberBruckCollGenerator::BruckCollective::~BruckCollective()
{
    ;
}

/******* EmberBruckCollGenerator (parent Ember motif) *******/
EmberBruckCollGenerator::EmberBruckCollGenerator(SST::ComponentId_t id, Params &params)
: EmberMessagePassingGenerator(id, params, "None"), m_tag(UINT_MAX)
{
    assert(0);
} 

EmberBruckCollGenerator::EmberBruckCollGenerator(SST::ComponentId_t id, Params &params, std::string name) 
: EmberMessagePassingGenerator(id, params, name), m_tag(UINT_MAX)
{
    ;
}

bool EmberBruckCollGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    assert(0);
}