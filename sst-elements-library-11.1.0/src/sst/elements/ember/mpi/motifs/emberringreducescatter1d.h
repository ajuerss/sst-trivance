
#ifndef _H_EMBER_ALLREDUCE1D_MOTIF
#define _H_EMBER_ALLREDUCE1D_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberRingReduceScatter1DGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberRingReduceScatter1DGenerator,
        "ember",
        "RingReduceScatter1DMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a RingReduceScatter1D operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.recvcount", "Sets the block size (buffers is n*recvcount, n=num processes)", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
    )

public:
	EmberRingReduceScatter1DGenerator(SST::ComponentId_t, Params& params);
    ~EmberRingReduceScatter1DGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    EmberRingAllreduce1D* m_reducescatter;
};

}
}

#endif
