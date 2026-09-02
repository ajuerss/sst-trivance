#include <sst_config.h>
#include "emberdlrm.h"
#include "emberswingcoll.h"


using namespace SST::Ember;

#define BOT_MLP_SIZE   49536
#define TOP_MLP_SIZE   728064 
#define EMB_ALL2ALL_SIZE   262144  //2048*128

// runtime in us (10E-6)
#define FWD_BOT_MLP (341*1000)
#define FWD_TOP_MLP (455*1000)
#define FWD_INTER (209*1000)
#define FWD_EMB (95*1000)

#define COMPUTE_STEP 100

bool EmberDLRMGenerator::generate(std::queue<EmberEvent *> &evQ)
{
    if(!startTimeTaken){
        startTimeTaken = true;
        enQ_getTime(evQ, &startTime);
    }
    switch(m_state)
    {
    case FWD:
    {
        enQ_compute(evQ, FWD_EMB / compscaling);
        count_compute_time += FWD_EMB / compscaling;
        m_state = ALLTOALL_1;
        // no break
    }
    case ALLTOALL_1:
    {
        if (!m_alltoall1->progress(evQ)) return false;
        m_state = FWDBWD;
        // no break
    }
    case FWDBWD:
    {
        enQ_compute(evQ, FWD_BOT_MLP / compscaling);
        enQ_compute(evQ, FWD_INTER / compscaling);
        enQ_compute(evQ, FWD_TOP_MLP / compscaling);
        enQ_compute(evQ, FWD_TOP_MLP*2 / compscaling);
        count_compute_time += FWD_BOT_MLP / compscaling;
        count_compute_time += FWD_INTER / compscaling;
        count_compute_time += FWD_TOP_MLP / compscaling;
        count_compute_time += FWD_TOP_MLP*2 / compscaling;
        m_state = ALLREDUCE;
        // no break
    }
    case ALLREDUCE:
    {
        bool allreduce_completed = m_allreduce1->progress(evQ);
        bool comp_left = m_comp_overlap < (FWD_INTER + FWD_BOT_MLP*2);

        if (comp_left) {
            enQ_compute(evQ, COMPUTE_STEP / compscaling);
            count_compute_time += COMPUTE_STEP / compscaling;
            m_comp_overlap += COMPUTE_STEP / compscaling;
        }
        
        if (!allreduce_completed) return false;

        comp_left = m_comp_overlap < (FWD_INTER + FWD_BOT_MLP*2);
        
        if (comp_left) { 
            // fast forward
            enQ_compute(evQ, ((FWD_INTER + FWD_BOT_MLP*2) - m_comp_overlap) / compscaling);
            count_compute_time += ((FWD_INTER + FWD_BOT_MLP*2) - m_comp_overlap) / compscaling;
            m_comp_overlap = (FWD_INTER + FWD_BOT_MLP*2) / compscaling;
        }

        m_state = ALLRED_ALLTOALL;
        // no break
    }
    case ALLRED_ALLTOALL:
    {
        if (m_progress_allreduce_2) {
            bool allred2_completed = m_allreduce2->progress(evQ);
            m_progress_allreduce_2 = !allred2_completed;
        }

        if (m_progress_alltoall_2) {
            bool alltoall2_completed = m_alltoall2->progress(evQ);
            m_progress_alltoall_2 = !alltoall2_completed;
        }

        if (m_progress_allreduce_2 || m_progress_alltoall_2) return false;
        enQ_getTime(evQ, &stopTime);
        return true;
    }
    default: assert(0);
    }

    assert(0);
}


EmberDLRMGenerator::EmberDLRMGenerator(SST::ComponentId_t id, Params &params)
    : EmberHxMeshGenerator(id, params, "DLRM"), m_progress_allreduce_2(true), m_progress_alltoall_2(true), m_state(FWD), m_comp_overlap(0)
{
    m_aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.0);
    px = (uint32_t)params.find("arg.px", 0);
    compscaling = (int)params.find("arg.compscaling", 1);
    
    // Check type of all reduce and if it is a valid one
    all_reduce_type = params.find<std::string>("arg.all_reduce_type", "Torus");
    if (all_reduce_type.compare("05D") && all_reduce_type.compare("2D") && all_reduce_type.compare("Rings")  && all_reduce_type.compare("Torus")) {
        printf("Error, non valid all reduce type!\n"); 
        exit(EXIT_FAILURE);
    }

    dimensions = (uint)params.find("arg.dimensions", 1);      
    std::string dimensions_sizes_s = params.find<std::string>("arg.dimensions_sizes", ""); 

    // Split the dimensions_sizes string into the value of each dimensions
    if(dimensions_sizes_s != ""){
        dimensions_sizes = (uint*) malloc(sizeof(uint)*dimensions);
        std::string tmp; 
        std::stringstream ss(dimensions_sizes_s);
        uint i = 0;
        while(getline(ss, tmp, ',')){
            if(i >= dimensions){
                std::cerr << "Too many dimensions sizes specified" << std::endl;
            }
            size_t index = dimensions - i - 1; // Dimensions are numbered in the reverse order
            dimensions_sizes[index] = std::stoul(tmp);
            ++i;
        }
        std::cout << "Dimensions: ";
        for(int i = dimensions - 1; i >= 0; i--){
            std::cout << dimensions_sizes[i] << " ";
        }
        std::cout << std::endl;
    }

    if (!all_reduce_type.compare("05D")) {
        m_allreduce1 = new EmberRingAllreduce05D(*this, TOP_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true);
        m_allreduce2 = new EmberRingAllreduce05D(*this, BOT_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true);
    } else if (!all_reduce_type.compare("2D")) {
        m_allreduce1 = new EmberRingAllreduce2D(*this, TOP_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true, px);
        m_allreduce2 = new EmberRingAllreduce2D(*this, BOT_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true, px);
    } else if (!all_reduce_type.compare("Torus")) {
        printf("\nUsing 2dRev\n"); fflush(stdout);
        float* m_data = NULL;
        int ports = dimensions*2;
        bool sync = true;
        //(EmberHxMeshGenerator &gen, uint32_t count, uint32_t rank, uint32_t comm_size, Communicator comm, double aggregation_cost_ns, bool nb, uint dimensions, uint* dimensions_sizes, RingType ring_type, float* data)
        printf("Using %s All Reduce - Size %d\n", all_reduce_type.c_str(), size());
        m_allreduce1 = new EmberRingAllreduceRev(*this, TOP_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, false, dimensions, dimensions_sizes, RING_ALLREDUCE, m_data);
        m_allreduce2 = new EmberRingAllreduceRev(*this, BOT_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, false, dimensions, dimensions_sizes, RING_ALLREDUCE, m_data);
    } else if (!all_reduce_type.compare("Rings")) {
        m_allreduce1 = new EmberRingAllreduce25D(*this, TOP_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true, px);
        m_allreduce2 = new EmberRingAllreduce25D(*this, BOT_MLP_SIZE, rank(), size(), GroupWorld, m_aggregation_cost_ns, true, px);
    } else {
        printf("Unexpected branch with all reduce type! Exiting!\n");
        exit(EXIT_FAILURE);
    }
    printf("Using %s All Reduce\n", all_reduce_type.c_str());

    m_alltoall1 = new EmberImprovedAlltoall(*this, EMB_ALL2ALL_SIZE/size(), rank(), size(), GroupWorld);
    m_alltoall2 = new EmberImprovedAlltoall(*this, EMB_ALL2ALL_SIZE/size(), rank(), size(), GroupWorld);
}

EmberDLRMGenerator::~EmberDLRMGenerator()
{
    delete m_alltoall1;
    delete m_alltoall2;
    delete m_allreduce1;
    delete m_allreduce2;
    printf("\nRank %d - Total Compute time %" PRIu64 "  Total: %" PRIu64 " (%" PRIu64 " - %" PRIu64 ") \n", rank(), count_compute_time, stopTime - startTime, stopTime, startTime);
}