#ifndef _H_EMBER_RESNET152_MOTIF
#define _H_EMBER_RESNET152_MOTIF

#include "mpi/embermpigen.h"
#include "emberswingcoll.h"

#define NUM_B 10
#define RUNS 1

namespace SST {
namespace Ember {

class EmberResNet152GeneratorSwing : public EmberSwingCollGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberResNet152GeneratorSwing,
        "ember",
        "ResNet152SwingMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Runs ResNet152",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.all_reduce_type", "All Reduce Type (SwingB | SwingL)", "SwingB"},
        {"arg.compscaling", "", "1"},
    )

public:
	EmberResNet152GeneratorSwing(SST::ComponentId_t, Params& params);
    ~EmberResNet152GeneratorSwing();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    bool generate_iteration(std::queue<EmberEvent *> &evQ);
    void temp_fix_all_reduce_size(int idx);

private:
    int allreduce_sizes[NUM_B] = {6511592, 6567936, 5905920, 6113280, 6176256, 6112768, 6176256, 6112768, 5321216, 5194816};
    //int allreduce_sizes[NUM_B] = {2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048};

    std::vector<SwingCollective*> m_allreduces;
    int m_bwd_rt_per_B;
    int m_fwd_rt_whole_model;
    int compute_step;

    // minibatch idx (relative to a single iteration)
    int m_i_b;

    // minibatch time. Used to count the time spent computing a minibatch. 
    // Gets reset when reaches m_bwd_rt_per_B
    int m_t_b;

    // global iteration index
    int m_iter;

    uint64_t count_compute_time = 0;

    double m_aggregation_cost_ns;
    uint32_t px = 0;
    std::string all_reduce_type;
    uint dimensions;    
    uint* dimensions_sizes = NULL;
    uint64_t startTime = 0, stopTime, pollingStart;
    bool startTimeTaken = false;
};

}
}

#endif
