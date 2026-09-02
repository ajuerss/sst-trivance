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

#define TAG 0xDEADBEEF


#include <sst_config.h>
#include "emberalltoallv.h"

using namespace SST::Ember;

EmberAlltoallvGenerator::EmberAlltoallvGenerator(SST::ComponentId_t id,
                                            Params& params) :
	EmberMessagePassingGenerator(id, params, "Alltoallv"),
    m_loopIndex(0)
{
	m_iterations = (uint32_t) params.find("arg.iterations", 1);
	m_count      = (uint32_t) params.find("arg.count", 1);
    m_messageSize = (uint32_t) params.find("arg.messageSize", 128);
    m_sendBuf = NULL;
    m_recvBuf = NULL;
}

bool EmberAlltoallvGenerator::generate( std::queue<EmberEvent*>& evQ) {

    if ( 0 == m_loopIndex ) {
        verbose(CALL_INFO, 1, 0, "rank=%d size=%d\n", rank(), size());
    }

    if ( rank() == 0) {
        enQ_send( evQ, m_sendBuf, m_messageSize, CHAR,
                3, TAG, GroupWorld );
    } else if (rank() == 3) {
        enQ_recv( evQ, m_recvBuf, m_messageSize, CHAR,
                0, TAG, GroupWorld, &m_resp );
    }


    if ( ++m_loopIndex == m_iterations ) {
        return true;
    } else {
        return false;
    }
}
