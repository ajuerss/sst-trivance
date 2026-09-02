// Copyright 2009-2021 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2021, NTESS
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
#include "emberallpingpong.h"

using namespace SST::Ember;

#define TAG 0xDEADBEEF

inline int mod( int a, int b )
{
    int tmp = ((a % b) + b) % b;
    return tmp;
}

EmberAllPingPongGenerator::EmberAllPingPongGenerator(SST::ComponentId_t id,
                                            Params& params) :
	EmberMessagePassingGenerator(id, params, "AllPingPong"),
    m_loopIndex(0)
{
	m_iterations = (uint32_t) params.find("arg.iterations", 1);
	m_messageSize = (long) params.find<long>("arg.messageSize", 128);
	m_computeTime = (uint32_t) params.find("arg.computetime", 1000);

    //printf("Message Size is %ld\n", m_messageSize);
    m_sendBuf = NULL;
    m_recvBuf = NULL;

    nextMRIndex = 0;
    msgRequests = (MessageRequest*) malloc(sizeof(MessageRequest) * (size()) * 2);
}

bool EmberAllPingPongGenerator::generate( std::queue<EmberEvent*>& evQ)
{
    if ( m_loopIndex == m_iterations ) {
        enQ_waitall( evQ, nextMRIndex, msgRequests, NULL );
        if ( 0 == rank()) {
            double totalTime = (double)(m_stopTime - m_startTime)/1000000000.0;

            double latency = ((totalTime/m_iterations)/2);
            double bandwidth = (double) m_messageSize / latency;

            output("%s: total time %.3f us, loop %d, bufLen %d"
                    ", latency %.3f us. bandwidth %f GB/s\n",
                                getMotifName().c_str(),
                                totalTime * 1000000.0, m_iterations,
                                m_messageSize,
                                latency * 1000000.0,
                                bandwidth / 1000000000.0 );
        }

        return true;
    }

    if ( 0 == m_loopIndex ) {
        verbose(CALL_INFO, 1, 0, "rank=%d size=%d\n", rank(), size());
        if ( 0 == rank() ) {
            enQ_getTime( evQ, &m_startTime );
        }
    }

    const bool participate = true;

    if( participate ) {

        for (int i = 1; i <= size(); i++) {
            if (mod(rank() - i, size()) != rank()) {
                //printf("Receiving in %d from %d \n",rank(), mod(rank() - i, size())); fflush(stdout);
                enQ_irecv( evQ, m_recvBuf, m_messageSize, CHAR,
                    mod(rank() - i, size()), TAG, GroupWorld, &msgRequests[nextMRIndex++]);
            } else {
                //printf("Edge Receiving from %d to %d \n",mod(rank() - i, size()), i); fflush(stdout);
            }
        }

        for (int i = 1; i <= size(); i++) {
            if (mod(rank() + i, size()) != rank()) {
               //printf("Sending from %d to %d \n",i, mod(rank() + i, size())); fflush(stdout);
                enQ_isend( evQ, m_sendBuf, m_messageSize, CHAR, mod(rank() + i, size()), TAG,
                                                    GroupWorld, &msgRequests[nextMRIndex++]);
            } else {
                //printf("Edge Sending from %d to %d \n",i, mod(rank() + i, size())); fflush(stdout);
            }
        }

        /*for (int i = 0; i < size(); i++) {
            if (i != rank()) {
                //printf("Receiving from %d to %d \n",mod(rank() - i, size()), i); fflush(stdout);
                enQ_irecv( evQ, m_recvBuf, m_messageSize, CHAR,
                    i, TAG, GroupWorld, &msgRequests[nextMRIndex++]);
            } else {
            }
        }

        for (int i = 0; i < size(); i++) {
            if (i != rank()) {
                enQ_isend( evQ, m_sendBuf, m_messageSize, CHAR, i, TAG,
                                                    GroupWorld, &msgRequests[nextMRIndex++]);
            } else {
            }
        }*/

    	if ( ++m_loopIndex == m_iterations ) {
        	enQ_getTime( evQ, &m_stopTime );
    	}

    }
    return false;
}
