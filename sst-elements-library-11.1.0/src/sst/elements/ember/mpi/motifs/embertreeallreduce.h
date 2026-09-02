#ifndef _H_EMBER_SWINGALLREDUCE_MOTIF
#define _H_EMBER_SWINGALLREDUCE_MOTIF

#include "mpi/embermpigen.h"

namespace SST {
namespace Ember {

class EmberTreeAllreduceGenerator : public EmberMessagePassingGenerator {

public:
    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        EmberTreeAllreduceGenerator,
        "ember",
        "TreeAllreduceMotif",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Performs an Allreduce operation with type set to FLOAT and operation SUM",
        SST::Ember::EmberGenerator
    )

    SST_ELI_DOCUMENT_PARAMS(
        {"arg.aggregation_cost_ns", "Cost to sum two floats", "0.01"},
        {"arg.count", "Sets the block size (buffers is n*count, n=num processes)", "1"},
        {"arg.validate", "When 1, the result will be validated", "0"},
        {"arg.ports", "How many ports to use", "1"},
    )

public:
	EmberTreeAllreduceGenerator(SST::ComponentId_t, Params& params);
    ~EmberTreeAllreduceGenerator();
    bool generate(std::queue<EmberEvent*>& evQ);

private:
    int m_recvcount;
    int m_validate;
    float* m_data;
    float* m_tmp_data;
    float* m_data_validation_send;
    float* m_data_validation_recv;
    bool m_validation_reduce_executed;
    MessageResponse m_resp;
    int m_mask;
};

}
}

#endif
