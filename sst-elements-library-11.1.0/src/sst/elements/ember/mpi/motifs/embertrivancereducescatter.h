#ifndef _H_EMBER_TRIVANCEREDUCESCATTER_MOTIF
#define _H_EMBER_TRIVANCEREDUCESCATTER_MOTIF

#include "mpi/embermpigen.h"
#include "embertrivancecoll.h"

namespace SST {
namespace Ember {

class EmberTrivanceReduceScatterGenerator : public EmberTrivanceCollGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberTrivanceReduceScatterGenerator,
        "ember",
        "TrivanceReduceScatterMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a TrivanceReduceScatter operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.recvcount", "Sets the block size (buffers is n*recvcount, n=num processes)", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
        {"arg.dimensions", "Topology dimensions", "1"},
        {"arg.ports", "How many ports to use", "1"},
        {"arg.sync", "If true, in the multiported allreduce, it waits for all the ports to be done before starting the next iteration.", "true"},
        {"arg.dimensions_sizes", "Comma-separated list of size of each dimension.", ""},
    )

public:
	EmberTrivanceReduceScatterGenerator(SST::ComponentId_t, Params& params);
    ~EmberTrivanceReduceScatterGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    TrivanceCollective* m_reducescatter;
};

}
}

#endif
