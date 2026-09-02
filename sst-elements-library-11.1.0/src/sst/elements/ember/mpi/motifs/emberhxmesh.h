
#ifndef _H_EMBER_ALLREDUCE_MOTIF
#define _H_EMBER_ALLREDUCE_MOTIF

#include "mpi/embermpigen.h"

namespace SST {
namespace Ember {

class EmberHxMeshGenerator : public EmberMessagePassingGenerator {

private:

    public:
        enum RingType {
            RING_ALLREDUCE = 0,
            RING_REDUCE_SCATTER,
            RING_ALLGATHER
        };

    // RingAllreduce engine. That's for 1D uni-directional. Used as building blocks to implement 1D and 2D (bi-directional) allreduces.
    class RingAllreduce {

    public:
        RingAllreduce(EmberHxMeshGenerator &gen, RingType ring_type, float *dst, uint32_t count, uint32_t vrank, uint32_t numproc, uint32_t recvfrom, uint32_t sendto, double aggregation_cost_ns, Communicator comm, bool validate=false);
        bool progress(std::queue<EmberEvent *> &evQ);
        bool hasPendingRecv();
        MessageRequest getRecvHandle();
        void notifyRecv();
        void setBuff(float *new_dest);
        void reset();
        float* getBuff();
        uint64_t getMovedBytes();
        void setEnable(bool enable);
        bool isEnabled();

    private:
        bool reduceScatter(std::queue<EmberEvent *> &evQ);
        bool allGather(std::queue<EmberEvent *> &evQ);
        bool pack(std::queue<EmberEvent *> &evQ);
        void aggregate(std::queue<EmberEvent *> &evQ, float* a, float* b, uint32_t num);
        uint32_t getBlockOffset(uint32_t block_idx);
        uint32_t getBlockSize(uint32_t block_idx);

    private:
        enum ring_allreduce_state_t {
            REDUCE_SCATTER = 0,
            ALL_GATHER,
            FINI
        };

    private:
        EmberHxMeshGenerator &m_gen;
        float *m_dst;
        float *m_tmp;
        float *m_send_tmp;
        uint32_t m_p;
        uint32_t m_r;
        uint32_t m_i;
        uint32_t m_recvfrom;
        uint32_t m_sendto;
        uint32_t m_tag1, m_tag2;
        uint32_t m_count;
        bool m_waiting_recv, m_ready_to_recv;
        double m_aggregation_cost_ns;
        ring_allreduce_state_t m_state;
        MessageRequest m_req_recv, m_req_send;
        uint64_t m_data_sent;
        Communicator m_comm;
        
        bool m_enabled;
        bool m_do_reduce_scatter;
        bool m_do_allgather;
        bool m_validate;
    };

    // Abstract class that defines an Allreduce implementation (e.g., 1D or 2D)
    class EmberRingAllreduce 
    {
    public:
        EmberRingAllreduce(EmberHxMeshGenerator &gen, int num_allreduces, uint32_t count, uint32_t rank, uint32_t comm_size, bool nonblocking);
        virtual bool progress(std::queue<EmberEvent *> &evQ) = 0;
        virtual void reset() = 0;
        bool progress_phase(std::queue<EmberEvent *> &evQ);
        void printStats();

    protected:
        std::vector<RingAllreduce> m_allreduces;
        std::vector<MessageRequest> m_active_recv_handles;
        std::vector<RingAllreduce*> m_active_allreduce_ptrs;

        EmberHxMeshGenerator &m_gen;
        int m_handle_idx;
        int m_recv_idx;
        bool m_nb;
        int m_has_new_recv;
        MessageResponse m_resp_recv;
        uint64_t m_stop_time, m_start_time;
        uint32_t m_count;
        uint32_t m_r, m_p;
    };

public:

    // Randomized alltoall
    class EmberRandAlltoall
    {
    public:
        EmberRandAlltoall(EmberHxMeshGenerator &gen, uint32_t sendcount, uint32_t numblocks, uint32_t rank, uint32_t comm_size, Communicator comm, bool nb, bool det);
        ~EmberRandAlltoall();
        bool progress(std::queue<EmberEvent *> &evQ);
        void reset();
        
    private:
        EmberHxMeshGenerator &m_gen;
        int m_r, m_p, m_sendcount, m_numblocks, m_tot_blocks, m_tag, m_it;
        int *m_send_block_idx;
        MessageRequest *m_req_recv;
        MessageRequest *m_req_send;
        Communicator m_comm;

        uint64_t m_time_debug;
    };


    // Improved alltoall
    class EmberImprovedAlltoall
    {
    public:
        EmberImprovedAlltoall(EmberHxMeshGenerator &gen, uint32_t sendcount, uint32_t rank, uint32_t comm_size, Communicator comm);
        ~EmberImprovedAlltoall();
        bool progress(std::queue<EmberEvent *> &evQ);
        int mod(int a, int b);
        void reset();
        
    private:
        EmberHxMeshGenerator &m_gen;
        int m_r, m_p, m_sendcount, m_tag;
        Communicator m_comm;

        uint64_t m_time_debug;
        uint32_t m_loopIndex = 0;
        uint32_t m_iterations = 1;
        uint32_t m_messageSize;
        uint64_t m_startTime;
        uint64_t m_stopTime;

        MessageRequest* msgRequests;
        int nextMRIndex = 0;

        MessageResponse m_resp;
        void*    m_sendBuf;
        void*    m_recvBuf;
    };


    // 0.5D Allreduce 
    class EmberRingAllreduce05D: public EmberRingAllreduce
    {
    public:
        EmberRingAllreduce05D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns = 0, bool nb = false, RingType ring_type = RING_ALLREDUCE);
        ~EmberRingAllreduce05D();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;

    private:
        void validate();

    private:
    enum allreduce_state_t {
        INIT,
        PHASE_1,   
        FINI
    };

    private:
        allreduce_state_t m_state;
        float* m_dstx_1;
    };


    // 1D Allreduce 
    class EmberRingAllreduce1D: public EmberRingAllreduce
    {
    public:
        EmberRingAllreduce1D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns = 0, bool nb = false, RingType ring_type = RING_ALLREDUCE);
        ~EmberRingAllreduce1D();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;

    private:
        void validate();

    private:
    enum allreduce_state_t {
        INIT,
        PHASE_1,   
        FINI
    };

    private:
        allreduce_state_t m_state;
        float* m_dstx_1;
        float* m_dstx_2;
    };

    // 2D Allreduce 
    class EmberRingAllreduce2D: public EmberRingAllreduce
    {
    public:
        EmberRingAllreduce2D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns = 0, bool nb = false, uint32_t px = 1, RingType ring_type = RING_ALLREDUCE);
        ~EmberRingAllreduce2D();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;
        
    private:
        void validate();

    private:
    enum allreduce_state_t {
        INIT,
        PHASE_1,   
        PHASE_2,
        FINI
    };

    private:
        uint32_t m_px, m_py;
        allreduce_state_t m_state;
        float *m_dstx_1;
        float *m_dstx_2;
        float *m_dsty_1;
        float *m_dsty_2;
    };

    // 25D Allreduce 
    class EmberRingAllreduce25D: public EmberRingAllreduce
    {
    public:
        EmberRingAllreduce25D(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns = 0, bool nb = false,  uint32_t px = 1, RingType ring_type = RING_ALLREDUCE);
        ~EmberRingAllreduce25D();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;

    private:
        void validate();
        std::pair<uint, uint> getRowColFromRank(uint rank); // Assumes ranks are allocated row by row
        uint getRankFromRowCol(std::pair<uint, uint> rowcol);        
        // Functions from the paper "Edge-disjoint Hamiltonian cycles in two-dimensional torus"
        std::pair<uint, uint> getx1x0(uint X);
        std::pair<uint, uint> getf0(uint X);
        std::pair<uint, uint> getf1(uint X);
        std::pair<uint, uint> getNeighRing0(uint rank);
        std::pair<uint, uint> getNeighRing1(uint rank);

    private:
    enum allreduce_state_t {
        INIT,
        PHASE_1,   
        FINI
    };

    private:
        uint32_t k0, k1;
        allreduce_state_t m_state;
        float* m_dst_1;
        float* m_dst_2;
        float* m_dst_3;
        float* m_dst_4;
    };

    // Revised Allreduce 
    class EmberRingAllreduceRev: public EmberRingAllreduce
    {
    public:
        EmberRingAllreduceRev(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns = 0, bool nb = false, uint dimensions = 2, uint* dimensions_sizes = NULL, RingType ring_type = RING_ALLREDUCE, float* data = NULL);
        ~EmberRingAllreduceRev();
        bool progress(std::queue<EmberEvent *> &evQ) override;
        void reset() override;
        
    private:
        void validate();

    private:
        uint m_dimensions;
        uint* m_dimensions_sizes;
        uint m_state;
        bool m_validate;
        float* m_data;
    };

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberHxMeshGenerator,
        "ember",
        "HxMeshMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "HxMesh parent motif -- this is meant to be a superclass of HxMesh motifs",
        SST::Ember::EmberGenerator
    )

public:
	EmberHxMeshGenerator(SST::ComponentId_t, Params& params);
	EmberHxMeshGenerator(SST::ComponentId_t, Params& params, std::string name);
    bool generate( std::queue<EmberEvent*>& evQ);

    uint32_t getNextTag() { return m_tag--; }

private:
    uint32_t m_tag;
};

}
}

#endif
