#ifndef _H_EMBER_TRIVANCEALLREDUCE_MOTIF
#define _H_EMBER_TRIVANCEALLREDUCE_MOTIF

#include "mpi/embermpigen.h"
#include "embertrivancecoll.h"

namespace SST {
namespace Ember {

class EmberTrivanceAllreduceGenerator : public EmberTrivanceCollGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberTrivanceAllreduceGenerator,
        "ember",
        "TrivanceAllreduceMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs an Allreduce operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.count", "Sets the block size (buffers is n*count, n=num processes)", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
        {"arg.validate", "When 1, the result will be validated", "1"},
        {"arg.ports", "How many ports to use", "1"},
        {"arg.sync", "If true, in the multiported allreduce, it waits for all the ports to be done before starting the next iteration.", "true"},
        {"arg.dimensions", "Topology dimensions", "1"},
        {"arg.dimensions_sizes", "Comma-separated list of size of each dimension.", ""},
        {"arg.latency_optimal", "If =1, will run the latency optimal version.", ""},
    )

public:
	EmberTrivanceAllreduceGenerator(SST::ComponentId_t, Params& params);
    ~EmberTrivanceAllreduceGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    TrivanceCollective* m_allreduce;
    int m_recvcount;
    int m_validate;
    float* m_data;
    float* m_data_validation_send;
    float* m_data_validation_recv;
    bool m_validation_reduce_executed;
};

}
}

#endif
