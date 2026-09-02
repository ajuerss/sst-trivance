#ifndef _H_EMBER_SWINGALLGATHER_MOTIF
#define _H_EMBER_SWINGALLGATHER_MOTIF

#include "mpi/embermpigen.h"
#include "emberswingcoll.h"

namespace SST {
namespace Ember {

class EmberSwingAllgatherGenerator : public EmberSwingCollGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberSwingAllgatherGenerator,
        "ember",
        "SwingAllgatherMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs an Allgather operation with type set to FLOAT",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.recvcount", "Sets the block size (buffers is n*recvcount, n=num processes)", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
        {"arg.dimensions", "Topology dimensions", "1"},
        {"arg.ports", "How many ports to use", "1"},    
        {"arg.sync", "If true, in the multiported allreduce, it waits for all the ports to be done before starting the next iteration.", "true"},
        {"arg.dimensions_sizes", "Comma-separated list of size of each dimension.", ""},
    )

public:
	EmberSwingAllgatherGenerator(SST::ComponentId_t, Params& params);
    ~EmberSwingAllgatherGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    SwingCollective* m_allgather;
};

}
}

#endif
