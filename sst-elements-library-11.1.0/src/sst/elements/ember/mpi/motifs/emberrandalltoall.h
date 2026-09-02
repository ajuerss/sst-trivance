
#ifndef _H_EMBER_ALLREDUCE1D_MOTIF
#define _H_EMBER_ALLREDUCE1D_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberRandAlltoallDGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberRandAlltoallDGenerator,
        "ember",
        "RandalltoallMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a Randalltoall operation with type set to FLOAT",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.count", "Sets the number of elements (floats) to reduce", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
    )

public:
	EmberRandAlltoallDGenerator(SST::ComponentId_t, Params& params);
    ~EmberRandAlltoallDGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    EmberRandAlltoall* m_alltoall;
};

}
}

#endif
