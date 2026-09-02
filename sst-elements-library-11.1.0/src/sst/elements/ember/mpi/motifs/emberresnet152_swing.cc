#include <sst_config.h>
#include "emberresnet152_swing.h"

using namespace SST::Ember;

#define COMPUTE_STEP 100

void EmberResNet152GeneratorSwing::temp_fix_all_reduce_size(int idx) {

    int current = allreduce_sizes[idx]; 
    int min_div = 4 * size();
    int res = current / min_div;
    allreduce_sizes[idx] = res * min_div;

    return;
}

bool EmberResNet152GeneratorSwing::generate_iteration(std::queue<EmberEvent *> &evQ)
{
    //printf("[%d] generate_iteration: m_i_b: %d; m_t_b: %d/%d\n", rank(), m_i_b, m_t_b, m_bwd_rt_per_B);
    if (m_i_b == 0)
    {
        // forward
        enQ_compute(evQ, m_fwd_rt_whole_model);
        count_compute_time += m_fwd_rt_whole_model;

        enQ_compute(evQ, m_bwd_rt_per_B);
        count_compute_time += m_bwd_rt_per_B;
    }

    // progress in-flight allreduces
    bool all_completed = true;
    for (auto &allreduce : m_allreduces)
        all_completed = all_completed && allreduce->progress(evQ);

    // backward
    if (m_i_b < NUM_B)
    {
        if (m_i_b == 0 || m_t_b >= m_bwd_rt_per_B)
        {
            printf("[%d] new allreduce: m_i_b: %d; size: %d\n", rank(), m_i_b, allreduce_sizes[m_i_b]);
            temp_fix_all_reduce_size(m_i_b);
            SwingCollective* new_allred;

            int ports = dimensions*2;
            int latency_optimal = 0;
            if (!all_reduce_type.compare("SwingL")) {
                latency_optimal = 1;
            }
            new_allred = new SwingCollective(*this, dimensions, ports, allreduce_sizes[m_i_b], rank(), size(), GroupWorld, m_aggregation_cost_ns, true, true, SWING_ALLREDUCE, NULL, NULL, latency_optimal);
            printf("Using %s All Reduce\n", all_reduce_type.c_str());

            // we need to progress it now otherwise simqueue becomes empty (also consisten with what an MPI_Iallreduce would do).
            new_allred->progress(evQ);
            m_allreduces.push_back(new_allred);

            enQ_getTime(evQ, &stopTime);
            enQ_compute(evQ, [&]() {
                pollingStart = stopTime;
                return 0;
            });

            m_i_b++;
            m_t_b = 0;
        }
        else
        {
            if (all_completed) {
                enQ_getTime(evQ, &stopTime);
                enQ_compute(evQ, [&]() {
                    m_t_b = stopTime - pollingStart;
                    count_compute_time += m_t_b;
                    return 0;
                });
                // there is nothing to progress -> fast forward
                enQ_compute(evQ, m_bwd_rt_per_B - m_t_b);
                enQ_compute(evQ, [&]() {
                    count_compute_time += m_bwd_rt_per_B - m_t_b;
                    printf("m_bwd_rt_per_B compute %d (%d - %d)\n", m_bwd_rt_per_B - m_t_b, m_bwd_rt_per_B, m_t_b); fflush(stdout);
                    m_t_b = m_bwd_rt_per_B;
                    return 0;
                });
                enQ_getTime(evQ, &stopTime);
            } else {
                enQ_compute(evQ, compute_step);
            }
        }

        return false;
    }
    else
    {
        return all_completed;
    }
}

bool EmberResNet152GeneratorSwing::generate(std::queue<EmberEvent *> &evQ)
{
    //printf("[%d] m_iter: %d/%d\n", rank(), m_iter, RUNS);
    if(!startTimeTaken){
        startTimeTaken = true;
        enQ_getTime(evQ, &startTime);
    }
    while (1) {
        if (m_iter >= RUNS) return true;
    
        bool it_complete = generate_iteration(evQ);
        if (it_complete)
        {
            printf("[%d] Iteration complete!\n", rank());
            m_i_b = 0;
            assert(m_t_b == 0);
            m_iter++;

            for (auto &allreduce : m_allreduces){
                allreduce->printStats();
                delete allreduce;
            }
            m_allreduces.clear();
            enQ_getTime(evQ, &stopTime);
            // loop back
        } else return false;
    }
}

EmberResNet152GeneratorSwing::EmberResNet152GeneratorSwing(SST::ComponentId_t id, Params &params)
    : EmberSwingCollGenerator(id, params, "ResNet152Swing"), m_i_b(0), m_t_b(0), m_iter(0)
{
    m_aggregation_cost_ns = (double)params.find("arg.aggregation_cost_ns", 0.01);
    px = (uint32_t)params.find("arg.px", 0);
    int compscaling = (int)params.find("arg.compscaling", 1);
    
    // Check type of all reduce and if it is a valid one
    all_reduce_type = params.find<std::string>("arg.all_reduce_type", "SwingB");
    if (all_reduce_type.compare("SwingB") && all_reduce_type.compare("SwingL")) {
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

    // Change compute time based on total number of nodes
    switch(size()) {
        case 16:
            m_bwd_rt_per_B = 23800 * 1e3;        // ns
            m_fwd_rt_whole_model = 119000 * 1e3; // ns
            break;
        case 256:
            m_bwd_rt_per_B = 23800 * 1e3;        // ns
            m_fwd_rt_whole_model = 119000 * 1e3; // ns
            break;
        case 512:
            m_bwd_rt_per_B = 12600 * 1e3;        // ns
            m_fwd_rt_whole_model = 63000 * 1e3; // ns
            break;
        case 1024:
            m_bwd_rt_per_B = 7200 * 1e3;        // ns
            m_fwd_rt_whole_model = 36000 * 1e3; // ns
            break;
        case 2048:
            m_bwd_rt_per_B = 5533 * 1e3;        // ns
            m_fwd_rt_whole_model = 27667 * 1e3; // ns
            break;
        default:
            printf("Number of nodes not supported\n"); 
            exit(EXIT_FAILURE);
        }
        compute_step = COMPUTE_STEP;

        m_bwd_rt_per_B /= compscaling;
        m_fwd_rt_whole_model /= compscaling;
        compute_step /= compscaling;
}

EmberResNet152GeneratorSwing::~EmberResNet152GeneratorSwing()
{
    printf("\nRank %d - Total Compute time %" PRIu64 "  Total: %" PRIu64 " (%" PRIu64 " - %" PRIu64 ") \n", rank(), count_compute_time, stopTime - startTime, stopTime, startTime);
}