// -*- mode: c++ -*-

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


#ifndef COMPONENTS_MERLIN_TOPOLOGY_HAMMING_H
#define COMPONENTS_MERLIN_TOPOLOGY_HAMMING_H

#include <sst/core/event.h>
#include <sst/core/link.h>
#include <sst/core/params.h>
#include <sst/core/rng/sstrng.h>

#include <string.h>
#include <vector>

#include "sst/elements/merlin/router.h"

namespace SST {
namespace Merlin {

class topo_hamming_event : public internal_router_event {
public:
    int dimensions;
    int routing_dim;
    int* dest_loc;

    topo_hamming_event() {}
    topo_hamming_event(int dim) {	dimensions = dim; routing_dim = 0; dest_loc = new int[dim]; }
    virtual ~topo_hamming_event() { delete[] dest_loc; }
    virtual internal_router_event* clone(void) override
    {
        topo_hamming_event* tte = new topo_hamming_event(*this);
        tte->dest_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions*sizeof(int));
        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        internal_router_event::serialize_order(ser);
        ser & dimensions;
        ser & routing_dim;

        if ( ser.mode() == SST::Core::Serialization::serializer::UNPACK ) {
            dest_loc = new int[dimensions];
        }

        for ( int i = 0 ; i < dimensions ; i++ ) {
            ser & dest_loc[i];
        }
    }

protected:

private:
    ImplementSerializable(SST::Merlin::topo_hamming_event)

};


class topo_hamming_init_event : public topo_hamming_event {
public:
    int phase;

    topo_hamming_init_event() {}
    topo_hamming_init_event(int dim) : topo_hamming_event(dim), phase(0) { }
    virtual ~topo_hamming_init_event() { }
    virtual internal_router_event* clone(void) override
    {
        topo_hamming_init_event* tte = new topo_hamming_init_event(*this);
        tte->dest_loc = new int[dimensions];
        memcpy(tte->dest_loc, dest_loc, dimensions*sizeof(int));
        return tte;
    }

    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        topo_hamming_event::serialize_order(ser);
        ser & phase;
    }

private:
    ImplementSerializable(SST::Merlin::topo_hamming_init_event)

};


class topo_hamming: public Topology {

public:

    SST_ELI_REGISTER_SUBCOMPONENT_DERIVED(
        topo_hamming,
        "merlin",
        "hamming",
        SST_ELI_ELEMENT_VERSION(1,0,0),
        "Multi-dimensional HxNet topology object",
        SST::Merlin::Topology)

    SST_ELI_DOCUMENT_PARAMS(
        {"hamming:board_shape",        "Shape of the hamming specified as the number of routers in each dimension, where each dimension is separated by a colon.  For example, 4x4"},
        {"hamming:width",        "Number of links between routers in each dimension, specified in same manner as for shape.  Default is 1 for HxNet."},
        {"hamming:local_ports",  "Number of endpoints attached to each router. Default is 1 for HxNet for board switches."},
        {"hamming:global_shape",        " Indicates the overall scheme of the topology and how many boards we have in total."},
        {"hamming:fat_tree_shape",        " Indicates how many switches we want in each level of the fatree."},

        {"board_shape",        "Shape of the hamming specified as the number of routers in each dimension, where each dimension is separated by a colon.  For example, 4x4"},
        {"width",        "Number of links between routers in each dimension, specified in same manner as for shape.  Default is 1 for HxNet."},
        {"local_ports",  "Number of endpoints attached to each router. Default is 1 for HxNet for board switches."},
        {"global_shape",        " Indicates the overall scheme of the topology and how many boards we have in total."},
        {"fat_tree_shape",        " Indicates how many switches we want in each level of the fatree."},
        {"algorithm",        " Indicates the routing algorithm. 'min-adaptive': Minimal adaptive routing algorithm. 'min-adaptive-nogl': Like min-adaptive, but it never uses global links if src and dst are both on the same board. Default is 'min-adaptive'"},
    )


private:
long traffic_per_port[128];
    int router_id;
    int* id_loc;

    int dimensions;
    int* dim_size;
    int* dim_width;

    // Board Shape 
    std::vector<int> board_shape; // Rows, Columns

    // Global Shape
    std::vector<int> global_shape; // Rows, Columns

    // Fat Tree Shape
    std::vector<int> fat_tree_shape; // Rows, Columns

    // Indicates the global ID of the switch. This includes board switches and fat tree switches.
    int global_switch_id ;

    // Indicates the local ID of the switch inside a board
    int local_switch_id;

    // Indicates the global position of the switch considering all boards (ex. [6, 8])
    std::vector<int> global_pos;

    // Indicates the local position of the switch inside a board (ex. [2, 1])
    std::vector<int> local_pos;

    // Indicates the position of a switch using the four coordinates [a, b, x, y] (ex. [1,2,3,4])
    std::vector<int> unique_pos;

    // Indicates what fat_tree a switch is part of (if it is not a board switch). 
    // This is a 2 items array where the first item is 0 if it is a row_fat_tree or 1 if it is a col_fat_tree. 
    // The second item is a number indicating the corresponding row/col. If board switch then the id is [-1,-1]
    std::vector<int> fat_tree_id;

    // Indicates the position of a switch within a fat tree. 
    // This is a 2 items array where the first item is the level (0 is edge level) and the second item the number of the switch 
    // from left to right 
    std::vector<int> fat_tree_pos; 

    // Number of ports going down for a given edge switch.
    int down_ports_edge;

    // true if only one switch as fat tree
    bool single_switch_fat_tree;

    // Number of links between pair of edge <-> core switches
    int num_links_pair;

    // Number of switches in the first level of the fat tree
    int switches_first_level;

    int (* port_start)[2]; // port_start[dim][direction: 0=pos, 1=neg]

    int num_ports;
    int num_local_ports;
    std::string routing_algo;
    int local_port_start;

    int num_vns;
    
public:
    topo_hamming(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns);
    ~topo_hamming();

    virtual void route_packet(int port, int vc, internal_router_event* ev);
    virtual void route_packet_mesh(int port, int vc, internal_router_event* ev);
    virtual void route_packet_tree(int port, int vc, internal_router_event* ev);
    virtual internal_router_event* process_input(RtrEvent* ev);

    virtual void routeInitData(int port, internal_router_event* ev, std::vector<int> &outPorts);
    virtual internal_router_event* process_InitData_input(RtrEvent* ev);

    virtual PortState getPortState(int port) const;
    // This function returns us the unique_pos explained above for any switch ID. 
    // This works only for id which are part of the mesh since that is where we have our "NICs".
    virtual std::vector<int> getUniqueBoardPosFromID(int switch_id);
    // Returns a bool indicating true if this global id is a mesh switch or false if a fat tree switch
    virtual bool isBoardSwitch(int switch_id);
    // Returns the port to reach that destination from the core switch
    virtual std::vector<int> getOutputPortFor(int fat_tree_destination);

    
    virtual int getEndpointID(int port);

    virtual void setOutputBufferCreditArray(int const* array, int vcs);
    virtual void setOutputQueueLengthsArray(int const* array, int vcs);

    virtual void getVCsPerVN(std::vector<int>& vcs_per_vn) {
        for ( int i = 0; i < num_vns; ++i ) {
            vcs_per_vn[i] = 3;
        }
    }
    
    // We assume that nodes are numbered by first filling the top-left board (first row, first column), 
    // then the second board on the same row (first row, second column), etc..
    // Within a board, the same process is adopted. First we fill the first row, then the second, etc..
    uint get_coord_board(uint addr);
    uint get_coord_within_board(uint addr);
    uint get_coord_board_col(uint addr);
    uint get_coord_board_row(uint addr);
    uint get_coord_col(uint addr);
    uint get_coord_row(uint addr);
    bool wrap_south(uint board_row_dest);
    bool wrap_north(uint board_row_dest);
    bool wrap_east(uint board_col_dest);
    bool wrap_west(uint board_col_dest);

    uint get_nrows(){
        return global_shape[0];
    }

    uint get_ncols(){
        return global_shape[1];
    }

    uint get_nrows_board(){
        return board_shape[0];
    }

    uint get_ncols_board(){
        return board_shape[1];
    }

    bool is_in_row_tree(){
        return fat_tree_id[0] == 0;
    }

    bool is_in_col_tree(){
        return fat_tree_id[0] == 1;
    }

    bool is_tree_edge_switch(){
        return fat_tree_pos[0] == 0;
    }

    bool is_tree_core_switch(){
        return fat_tree_pos[0] == 1;
    }

    char port_id_to_mnemonic(uint id){
        switch(id){
            case 0: return 'N';
            case 1: return 'E';
            case 2: return 'S';
            case 3: return 'W';
            case 4: return 'C'; // NIC
            default:
                throw std::runtime_error("Unkown port ID");
        }
    }

    uint port_mnemonic_to_id(char mn){
        switch(mn){
            case 'N': return 0;
            case 'E': return 1;
            case 'S': return 2;
            case 'W': return 3;
            case 'C': return 4;
            default:
                throw std::runtime_error("Unkown port mnemonic");
        }
    }

protected:
    virtual int choose_multipath(int start_port, int num_ports, int dest_dist);

private:
    int num_vcs;
    int port_used[4];
    int const* output_credits;
    int const* output_queue_lengths;
    void idToLocation(int id, int *location) const;
    void parseDimString(const std::string &shape, int *output) const;
    std::vector<int> genericShapeParser(const std::string &shape, char symbol, int items) const;
    int get_dest_router(int dest_id) const;
    int get_dest_local_port(int dest_id) const;


};

}
}

#endif // COMPONENTS_MERLIN_TOPOLOGY_HAMMING