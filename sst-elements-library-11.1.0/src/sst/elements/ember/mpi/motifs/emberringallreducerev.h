
#ifndef _H_EMBER_ALLREDUCE2D_MOTIF
#define _H_EMBER_ALLREDUCE2D_MOTIF

#include "mpi/embermpigen.h"
#include "emberhxmesh.h"

namespace SST {
namespace Ember {

class EmberRingAllreduceRevGenerator : public EmberHxMeshGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberRingAllreduceRevGenerator,
        "ember",
        "RingAllreduceRevMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs a RingAllreduceRev operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.count", "Sets the number of elements (floats) to reduce", "1"},
        {"arg.blocking", "Blocking vs non-blocking", "true"},
        {"arg.concurrent", "Num concurrent allreduces", "1"},
        {"arg.validate", "When 1, the result will be validated", "0"},
        {"arg.dimensions", "Topology dimensions", "1"},
        {"arg.dimensions_sizes", "Comma-separated list of size of each dimension.", ""},
    )

public:
	EmberRingAllreduceRevGenerator(SST::ComponentId_t, Params& params);
    ~EmberRingAllreduceRevGenerator();
    bool generate( std::queue<EmberEvent*>& evQ);

private:
    std::vector<EmberRingAllreduceRev*> m_allreduce;
    int m_validate;
    uint32_t m_recvcount;
    float* m_data;
    float* m_data_validation_send;
    float* m_data_validation_recv;
    bool m_validation_reduce_executed;
};

}
}

#endif
