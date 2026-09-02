
#ifndef _H_EMBER_ALLREDUCE1D_MOTIF
#define _H_EMBER_ALLREDUCE1D_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberRingAllreduce05DGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberRingAllreduce05DGenerator,
        "ember",
        "RingAllreduce05DMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a RingAllreduce0.5D operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.count", "Sets the number of elements (floats) to reduce", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
    )

public:
	EmberRingAllreduce05DGenerator(SST::ComponentId_t, Params& params);
    ~EmberRingAllreduce05DGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    EmberRingAllreduce05D* m_allreduce;
    uint64_t start_time_local, stop_time_local;
    uint32_t count_local;
};

}
}

#endif
