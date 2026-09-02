
#ifndef _H_EMBER_ALLREDUCE2D_MOTIF
#define _H_EMBER_ALLREDUCE2D_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberRingAllreduce2DGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberRingAllreduce2DGenerator,
        "ember",
        "RingAllreduce2DMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a RingAllreduce2D operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.count", "Sets the number of elements (floats) to reduce", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
        {"arg.concurrent", "Num concurrent allreduces", "1"}
    )

public:
	EmberRingAllreduce2DGenerator(SST::ComponentId_t, Params& params);
    ~EmberRingAllreduce2DGenerator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    std::vector<EmberRingAllreduce2D*> m_allreduce;
};

}
}

#endif
