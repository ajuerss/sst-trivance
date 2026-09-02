#ifndef _H_EMBER_RESNET152_MOTIF
#define _H_EMBER_RESNET152_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"
#include "emberswingcoll.h"
#include "emberswingallreduce.h"

#define NUM_B 10
#define RUNS 1

namespace SST {
namespace Ember {

class EmberDLRMSwingGenerator : public EmberSwingCollGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberDLRMSwingGenerator,
        "ember",
        "DLRMSwingMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Runs DLRM",
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
	EmberDLRMSwingGenerator(SST::ComponentId_t, Params& params);
    ~EmberDLRMSwingGenerator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    bool generate_iteration(std::queue<EmberEvent *> &evQ);

private:
    enum dlrm_state_t {
        FWD = 0,
        ALLTOALL_1,
        FWDBWD, 
        ALLREDUCE,
        ALLRED_ALLTOALL
    };

private:
   

    // global iteration index
    int m_iter;

    uint64_t count_compute_time = 0;

    bool m_progress_allreduce_2;
    bool m_progress_alltoall_2;

    uint64_t m_comp_overlap;
    dlrm_state_t m_state;

    EmberImprovedAlltoall* m_alltoall1;
    EmberImprovedAlltoall* m_alltoall2;

    SwingCollective* m_allreduce1S;
    SwingCollective* m_allreduce2S;

    double m_aggregation_cost_ns;
    uint32_t px = 0;
    std::string all_reduce_type;
    uint dimensions;    
    uint* dimensions_sizes = NULL;
    uint64_t startTime = 0, stopTime, pollingStart;
    bool startTimeTaken = false;
    int compscaling;
};

}
}

#endif
