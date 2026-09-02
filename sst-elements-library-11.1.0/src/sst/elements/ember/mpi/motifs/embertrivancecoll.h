
#ifndef _H_EMBER_TRIVANCECOLL_MOTIF
#define _H_EMBER_TRIVANCECOLL_MOTIF

#include "mpi/embermpigen.h"

namespace SST {
namespace Ember {

class EmberTrivanceCollGenerator : public EmberMessagePassingGenerator {

private:

    public:
        enum CollType {
            TRIVANCE_ALLREDUCE = 0,
            TRIVANCE_REDUCE_SCATTER,
            TRIVANCE_ALLGATHER
        };

    // TrivanceCollectiveEngine engine. Used as building blocks to implement multidimensional collectives.
    class TrivanceCollectiveEngine {

    public:
        TrivanceCollectiveEngine(EmberTrivanceCollGenerator &gen, CollType coll_type, uint* dimensions, uint dimensions_num,
                        float *dst, uint32_t count, uint32_t vrank, uint32_t numproc, 
                        double aggregation_cost_ns, Communicator comm, bool validate, uint port, bool latency_optimal);
        bool progress(std::queue<EmberEvent *> &evQ);
        bool hasPendingRecv();
        MessageRequest getRecvHandle();
        void notifyRecv();
        void setBuff(float *new_dest);
        void reset();
        bool m_prev_step_active;
        float* getBuff();
        uint64_t getMovedBytes();
        void setEnable(bool enable);
        bool isEnabled();
        uint32_t getStep(){return m_i;}

    private:
        void getCoordFromId(int id, int* coord);
        int getIdFromCoord(int* coord, uint* dimensions, uint dimensions_num);
        void computeBlocksBitmap(int sender, int step, uint8_t* blocks_bitmap);        
        int getPeer(int sender, int step, CollType collective);
        int getPeer_o(int sender, int step, CollType collective);
        bool collective(std::queue<EmberEvent *> &evQ, CollType coll_type);
        bool pack(std::queue<EmberEvent *> &evQ);
        void processReceivedData(std::queue<EmberEvent *> &evQ, CollType collective);
        uint8_t** getBitmaps(int rank);
        uint32_t getBlockOffset(uint32_t block_idx);
        uint32_t getBlockSize(uint32_t block_idx);

    private:
        enum ring_allreduce_state_t {
            REDUCE_SCATTER = 0,
            ALL_GATHER,
            FINI
        };

    private:
        EmberTrivanceCollGenerator &m_gen;
        float *m_dst;
        float *m_tmp;
        float *m_send_tmp;
        uint32_t m_p;
        uint32_t m_r;
        uint32_t m_i;
        uint32_t m_tag1, m_tag2;
        uint32_t m_count;
        bool m_waiting_recv, m_ready_to_recv;
        double m_aggregation_cost_ns;
        ring_allreduce_state_t m_state;
        MessageRequest m_req_recv, m_req_send;
        uint64_t m_data_sent;
        Communicator m_comm;
        uint8_t *m_blocks_bitmap_s; // Bitmap representing the blocks I sent
        uint8_t *m_blocks_bitmap_r; // Bitmap representing the blocks I received
        
        uint8_t **m_my_blocks_matrix;
        // For each rank and for each step I have a bitmap of blocks rank 'rank' sends at step 'step': [rank][step][blocks]. From those I can get also blocks to recv etc
        std::vector<std::vector<std::vector<uint>>> m_blocks;
        
        uint32_t m_recv_size, m_send_size;
        uint *m_dimensions;
        uint m_dimensions_num;
        bool m_validate;
        uint m_port;
        uint** m_peers;
        uint** m_peers_o;
        uint** m_peers_a;
        bool m_enabled;
        bool m_latency_optimal;
                
        bool m_do_reduce_scatter;
        bool m_do_allgather;
    };

    // Abstract class that defines a generic trivance collective implementation running multiple collectives concurrently
    class TrivanceCollectiveRunner 
    {
    public:
        TrivanceCollectiveRunner(EmberTrivanceCollGenerator &gen, int num_allreduces, uint32_t count, uint32_t rank, uint32_t comm_size, 
                              bool nonblocking, bool sync);
        virtual bool progress(std::queue<EmberEvent *> &evQ) = 0;
        virtual void reset() = 0;
        bool progress_phase(std::queue<EmberEvent *> &evQ);
        void printStats();

    protected:
        std::vector<TrivanceCollectiveEngine> m_allreduces;
        std::vector<MessageRequest> m_active_recv_handles;
        std::vector<TrivanceCollectiveEngine*> m_active_allreduce_ptrs;

        EmberTrivanceCollGenerator &m_gen;
        int m_handle_idx;
        int m_recv_idx;
        bool m_nb, m_sync;
        int m_has_new_recv;
        MessageResponse m_resp_recv;
        uint64_t m_stop_time, m_start_time;
        uint32_t m_count;
        uint32_t m_r, m_p;
        bool m_latency_optimal;
        uint  m_dimensions_num;
        uint* m_dimensions;

    };

public:
    // TrivanceCollective
    // This is a trivance collective on a d-dimensional torus.
    // It is 'Single' in the sense that there is a single collective running on m_count elements.
    class TrivanceCollective: public TrivanceCollectiveRunner
    {
    public:
        TrivanceCollective(EmberTrivanceCollGenerator &gen, uint dimensions_num, uint ports,
                        uint32_t count, uint32_t rank, uint32_t comm_size, 
                        Communicator comm, double aggregation_cost_ns = 0, bool nb = false,
                        bool sync = true, CollType coll_type = TRIVANCE_ALLREDUCE, float* data = NULL,
                        uint* dimensions = NULL, bool latency_optimal = false);
        ~TrivanceCollective();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;
    private:
    enum allreduce_state_t {
        INIT,
        PHASE_1,   
        FINI
    };

    private:
        allreduce_state_t m_state;
        CollType m_coll_type;
    };

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberTrivanceCollGenerator,
        "ember",
        "EmberTrivanceMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "EmberTrivanceMotif parent motif -- this is meant to be a superclass of EmberTrivanceMotif motifs",
        SST::Ember::EmberGenerator
    )

public:
	EmberTrivanceCollGenerator(SST::ComponentId_t, Params& params);
	EmberTrivanceCollGenerator(SST::ComponentId_t, Params& params, std::string name);
    bool generate( std::queue<EmberEvent*>& evQ);

    uint32_t getNextTag() { return m_tag--; }

private:
    uint32_t m_tag;    
};

}
}

#endif
