// Copyright 2009-2020 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2020, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <sst_config.h>

#include <sst/core/rng/xorshift.h>
#include "emberscatter.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fstream>
#include <time.h>
#define _POSIX_C_SOURCE 1
#include <limits.h>
#include <stdio.h>  // defines FILENAME_MAX

#define TAG 0xDEADBEEF

using namespace SST::Ember;
using namespace SST::RNG;


EmberScatterGenerator::EmberScatterGenerator(SST::ComponentId_t id, Params& params) :
  EmberMessagePassingGenerator(id, params) {

	msgSize = (uint32_t) params.find("arg.messageSize", 1800);
    load_factor = (float) params.find("arg.load_delay", 0);
	maxIterations = (uint32_t) params.find("arg.iterations", 1);
    waitToStart = params.find("arg.waitToStart", 0 );
    is_dragonfly = (uint32_t) params.find("arg.is_dragonfly", 1);


    // Read List of recv to do
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Current working dir: %s\n", cwd);  fflush(stdout);
    } else {
        perror("getcwd() error");
    }
    std::string name_file;
    if (is_dragonfly == 1) {
        name_file = "input_dragonfly/read_";
    } else if (is_dragonfly == 0) {
        name_file = "input_generic/read_";
    }
    name_file = name_file + std::to_string(rank());
    name_file = name_file + ".csv";
    FILE *stream = fopen(name_file.c_str(), "r");
    if (stream == NULL) {
        printf("Error opening file: \n"); fflush(stdout);
    }

    int total_bytes_recv = 0;
    char buffer[1024];
    while (fgets(buffer, 1024, stream)) {
        char *token = strtok(buffer, ",");
        int idx = 0;
        int rank_msg, size_msg, sleep_msg;
        while (token) { 
            // Just printing each integer here but handle as needed
            int n = atoi(token);         
            if (idx == 0) {
                rank_msg = n;
            } else if (idx == 1) {
                size_msg = n;
            } else if (idx == 2) {
                sleep_msg = n;
            } else {
                printf("Error");
                exit(0);
            }
            idx++;
            token = strtok(NULL, ",");
        }
        total_bytes_recv += size_msg;
        receiving_list.push_back(EventEntry(rank_msg, size_msg, sleep_msg));
        idx = 0;
    }

    // Read List of send to do
    if (is_dragonfly == 1) {
        name_file = "input_dragonfly/send_";
    } else if (is_dragonfly == 0) {
        name_file = "input_generic/send_";
    }
    name_file = name_file + std::to_string(rank());
    name_file = name_file + ".csv";
    FILE *stream_2 = fopen(name_file.c_str(), "r");
    if (stream == NULL) {
        printf("Error opening file: \n"); fflush(stdout);
    }
    int num_recv = 0;

    char buffer_2[1024];
    while (fgets(buffer_2, 1024, stream_2)) {
        char *token = strtok(buffer_2, ",");
        int idx = 0;
        int rank_msg, size_msg, sleep_msg;
        while (token) { 
            // Just printing each integer here but handle as needed
            int n = atoi(token);
            
            if (idx == 0) {
                rank_msg = n;
            } else if (idx == 1) {
                size_msg = n;
            } else if (idx == 2) {
                sleep_msg = n;
            } else {
                printf("Error while parsing CSV files, rank=%d\n", rank());
                exit(0);
            }
            idx++;
            token = strtok(NULL, ",");
        }
        sending_list.push_back(EventEntry(rank_msg, size_msg, sleep_msg));
        idx = 0;
    }

    num_recv = receiving_list.size();
    int num_event = sending_list.size() + num_recv;
    msgRequests = (MessageRequest*) malloc(sizeof(MessageRequest) * num_event);
    if (total_bytes_recv > 0) {
       // m_recvBuf = memAlloc(total_bytes_recv);
    }
    
}

bool EmberScatterGenerator::generate( std::queue<EmberEvent*>& evQ ) {

    if (iteration == 0) {
        enQ_barrier(evQ, GroupWorld);
        srand(time(NULL)+rank());
        //enQ_compute(evQ, rand() % 10000);
        if ( 0 <= rank() ) {
            enQ_getTime( evQ, &m_startTime );
        }
    }

    if ( iteration == maxIterations ) {
        if (nextMRIndex > 0) {
            enQ_waitall( evQ, nextMRIndex, msgRequests, NULL );
        }
        if ( 0 <= rank() ) {
            enQ_getTime( evQ, &m_stopTime );
            enQ_compute(evQ, [&]() {
                double totalTime = (double)(m_stopTime - m_startTime)/1000.0;
                printf("Rank %d - Total Time %f (us) - Start %ld Stop %ld\n", rank(), totalTime, m_startTime, m_stopTime);
                return 0;
            });
        }
        return true;
    }

    //char* current_start_buf = (char*)m_recvBuf;
    for (int idx_rec = 0;  idx_rec < receiving_list.size(); idx_rec++) {
        enQ_irecv( evQ, m_recvBuf, receiving_list[idx_rec].size, CHAR,
                   receiving_list[idx_rec].rank, TAG, GroupWorld, &msgRequests[nextMRIndex++]);
        //enQ_barrier(evQ, GroupWorld);
        //current_start_buf = current_start_buf + receiving_list[idx_rec].size;
    }

    for (int idx_rec = 0;  idx_rec < sending_list.size(); idx_rec++) {
        //m_sendBuf = memAllocCustomChar(sending_list[idx_rec].size , 'A');
        enQ_isend( evQ, m_sendBuf, sending_list[idx_rec].size, CHAR, sending_list[idx_rec].rank, TAG,
                                                GroupWorld, &msgRequests[nextMRIndex++]);
        //enQ_barrier(evQ, GroupWorld);
        uint64_t delay = sending_list[idx_rec].size / 25;
        enQ_compute(evQ, (uint64_t)delay * load_factor);
    }

    iteration++;

    return false;
} 
