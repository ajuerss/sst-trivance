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

#ifndef _H_EMBER_TESTANY
#define _H_EMBER_TESTANY

#include "mpi/embermpigen.h"
#include "rng/xorshift.h"

namespace SST
{
    namespace Ember
    {

        class EmberTestanyGenerator : public EmberMessagePassingGenerator
        {

        public:
            SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
                EmberTestanyGenerator,
                "ember",
                "TestanyMotif",
                SST_ELI_ELEMENT_VERSION(1, 0, 0),
                "Performs a Testany Motif",
                SST::Ember::EmberGenerator)
            SST_ELI_DOCUMENT_STATISTICS(
                {"time-Init", "Time spent in Init event", "ns", 0},
                {"time-Finalize", "Time spent in Finalize event", "ns", 0},
                {"time-Rank", "Time spent in Rank event", "ns", 0},
                {"time-Size", "Time spent in Size event", "ns", 0},
                {"time-Send", "Time spent in Recv event", "ns", 0},
                {"time-Recv", "Time spent in Recv event", "ns", 0},
                {"time-Irecv", "Time spent in Irecv event", "ns", 0},
                {"time-Isend", "Time spent in Isend event", "ns", 0},
                {"time-Wait", "Time spent in Wait event", "ns", 0},
                {"time-Waitall", "Time spent in Waitall event", "ns", 0},
                {"time-Waitany", "Time spent in Waitany event", "ns", 0},
                {"time-Test", "Time spent in Test event", "ns", 0},
                {"time-Testany", "Time spent in Testany event", "ns", 0},
                {"time-Compute", "Time spent in Compute event", "ns", 0},
                {"time-Barrier", "Time spent in Barrier event", "ns", 0},
                {"time-Alltoallv", "Time spent in Alltoallv event", "ns", 0},
                {"time-Alltoall", "Time spent in Alltoall event", "ns", 0},
                {"time-Allreduce", "Time spent in Allreduce event", "ns", 0},
                {"time-Reduce", "Time spent in Reduce event", "ns", 0},
                {"time-Bcast", "Time spent in Bcast event", "ns", 0},
                {"time-Gettime", "Time spent in Gettime event", "ns", 0},
                {"time-Commsplit", "Time spent in Commsplit event", "ns", 0},
                {"time-Commcreate", "Time spent in Commcreate event", "ns", 0}, )

        public:
            EmberTestanyGenerator(SST::ComponentId_t id, Params &params) : EmberMessagePassingGenerator(id, params, "Null"), m_phase(Init)
            {
                m_rng = new SST::RNG::XORShiftRNG();
                m_to_recv = 0;
                tmp.resize(4);
            }
            bool generate(std::queue<EmberEvent *> &evQ)
            {
                switch ( m_phase ) {
                    case Init:
                        m_req.resize(4);
                        m_req_send.resize(4);

                        for (int i=0; i < 4; i++) {
                            uint32_t rank_recv_from = (rank() - 1 + size()) % size();
                            enQ_irecv(evQ, NULL, 13000, CHAR, rank_recv_from, 0xdeadbeef, GroupWorld, &m_req[i]);
                            m_to_recv++;
                        }

                        for (int i=0; i < 4; i++) {
                            uint32_t rank_send_to = (rank() + 1 + size()) % size();
                            //enQ_isend(evQ, NULL, 1048576, CHAR, rank_send_to, 0xdeadbeef, GroupWorld, &m_req_send[i]);
                            enQ_send(evQ, NULL, 13000, CHAR, rank_send_to, 0xdeadbeef, GroupWorld);
                        }
                        for (int i=0; i<4; i++) m_flag[i] = 0;
                        m_phase = Check;
                        return false;

                    case Check:

                        for (int i=0; i<4; i++) {
                            if (m_flag[i] == 1) {
                                m_flag[i] = -1;
                                m_to_recv--;
                                printf("[%d] index=%d src=%d left=%d\n", rank(), i, m_resp[i].src, m_to_recv);
                                if (m_to_recv==0) return true;
                            }
                        }

                        printf("[%d] testany\n", rank());

                        enQ_compute( evQ, 1000 );

                        //for (int i=0; i<4; i++) tmp[i] = m_req[i];
                        //enQ_testany( evQ, tmp.size(), &tmp[0], &m_indx, &m_flag, &m_resp );

                        //enQ_testany( evQ, m_req.size(), &m_req[0], &m_indx, &m_flag, &m_resp );

                        //if (m_flag[0]==0) enQ_test( evQ, &m_req[0], &m_flag[0], &m_resp[0]);
                        //if (m_flag[1]==0) enQ_test( evQ, &m_req[1], &m_flag[1], &m_resp[1]);
                        //if (m_flag[2]==0) enQ_test( evQ, &m_req[2], &m_flag[2], &m_resp[2]);
                        //if (m_flag[3]==0) enQ_test( evQ, &m_req[3], &m_flag[3], &m_resp[3]);


                        //enQ_waitany( evQ, m_req.size(), &m_req[0], &m_indx, &m_resp );
                        //m_flag = true;

                        return false;

                }
                assert(0);

                
                /*
                if (rank() > 1)
                    return true;

                if (rank() == 0)
                {
                    switch (m_phase)
                    {
                    case Init:
                        m_req_send.resize(1);
                        m_req_send[0] = 0;
                        printf("[%d] send\n", rank());
                        enQ_isend(evQ, NULL, 10485760, CHAR, 1, 0xdeadbeef, GroupWorld, &m_req_send[0]);
                        m_phase = Check;
                        return false;
                    case Check:
                        printf("[%d] wait (%p)\n", rank(), m_req_send[0]);
                        enQ_wait(evQ, &m_req_send[0]);
                        return true;
                    }
                }
                else
                {
                    switch (m_phase)
                    {
                    case Init:
                        m_req.resize(1);
                        m_req[0] = 0;
                        printf("[%d] recv\n", rank());
                        enQ_irecv(evQ, NULL, 10485760, CHAR, 0, 0xdeadbeef, GroupWorld, &m_req[0]);
                        m_phase = Check;
                        return false;
                    case Check:
                        printf("[%d] wait (%p)\n", rank(), m_req[0]);
                        enQ_wait(evQ, &m_req[0]);
                        return true;
                    }
                }
                assert(0);
                */
            }

        private:
            unsigned int getSeed()
            {
                struct timeval start;
                gettimeofday(&start, NULL);
                return start.tv_usec;
            }

            SST::RNG::XORShiftRNG *m_rng;
            int m_flag[4];
            int m_indx;
            int m_to_recv;
            enum
            {
                Init,
                Check
            } m_phase;
            std::vector<MessageRequest> tmp;
            std::vector<MessageRequest> m_req;
            std::vector<MessageRequest> m_req_send;
            MessageResponse m_resp[4];
        };

    }
}

#endif
