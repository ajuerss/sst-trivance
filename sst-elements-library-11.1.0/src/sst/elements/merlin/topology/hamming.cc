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
#include "hamming.h"
#include <math.h>       /* ceil */


#include "sst/core/rng/xorshift.h"

#include <algorithm>
#include <stdlib.h>

//#define DEBUG_HAMMING

#ifdef DEBUG_HAMMING
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

using namespace SST::Merlin;

static std::string get_string_from_array(std::vector<int> arr, char symbol) {
    std::string str;
    for (int i = 0; i < arr.size(); ++i) {
        str += std::to_string(arr[i]);
        if (i + 1 != arr.size()) { 
            str += symbol; 
        }
    }
    return str;
}

static std::string get_string_from_array(std::vector<char> arr, char symbol) {
    std::string str;
    for (int i = 0; i < arr.size(); ++i) {
        str += std::string(1, arr[i]);
        if (i + 1 != arr.size()) { 
            str += symbol; 
        }
    }
    return str;
}

topo_hamming::topo_hamming(ComponentId_t cid, Params& params, int num_ports, int rtr_id, int num_vns) :
    Topology(cid),
    router_id(rtr_id),
    num_ports(num_ports),
    num_vns(num_vns)
{
    memset(traffic_per_port, 0, sizeof(traffic_per_port));
    // Get the various parameters
    std::string shape;
    shape = params.find<std::string>("board_shape");
    std::string glob_shape = params.find<std::string>("global_shape");
    std::string fat_tree_shape_string = params.find<std::string>("fat_tree_shape");

    // Retrieve parameters and print them for debug
    is_board_switch = params.find<bool>("is_board_switch");
    single_switch_fat_tree = params.find<bool>("single_switch_fat_tree");
    global_switch_id = params.find<int>("global_switch_id");
    local_switch_id = params.find<int>("local_switch_id");
    params.find_array<int>("global_pos", global_pos);
    params.find_array<int>("local_pos", local_pos);
    params.find_array<int>("unique_pos", unique_pos);
    params.find_array<int>("fat_tree_id", fat_tree_id); 
    params.find_array<int>("fat_tree_pos", fat_tree_pos); 
    num_local_ports = params.find<int>("local_ports", 1);
    routing_algo = params.find<std::string>("algorithm", "min-adaptive");
    num_links_pair = params.find<int>("link_width", -1);
    switches_first_level = params.find<int>("switches_first_level", -1);
    is_hx = true;

    // Need to parse the shape string to get the number of dimensions
    // and the size of each dimension
    dimensions = std::count(shape.begin(),shape.end(),'x') + 1;

    dim_size = new int[dimensions];
    dim_width = new int[dimensions];
    port_start = new int[dimensions][2];

    parseDimString(shape, dim_size);

    global_shape = genericShapeParser(glob_shape, 'x', std::count(glob_shape.begin(),glob_shape.end(),'x') + 1);
    board_shape = genericShapeParser(shape, 'x', std::count(shape.begin(),shape.end(),'x') + 1);
    fat_tree_shape = genericShapeParser(fat_tree_shape_string, ':', std::count(fat_tree_shape_string.begin(),fat_tree_shape_string.end(),':') + 1);

    DEBUG_PRINT(("--- Num Ports This routers %d, local %d ---\n",num_ports, num_local_ports));
    DEBUG_PRINT(("--- Global Shape %s ---\n", glob_shape.c_str()));
    DEBUG_PRINT(("--- Global Shape1 %s ---\n", get_string_from_array(global_shape, 'x').c_str()));
    DEBUG_PRINT(("--- Board Shape is %s ---\n", shape.c_str()));
    DEBUG_PRINT(("--- Board Shape1 is %s ---\n", get_string_from_array(board_shape, 'x').c_str()));
    DEBUG_PRINT(("--- Fat Tree Shape is %s ---\n", get_string_from_array(fat_tree_shape, ':').c_str()));
    DEBUG_PRINT(("--- Routing used is %s ---\n", routing_algo.c_str()));
    DEBUG_PRINT(("--- Is Board Switch is %d ---\n", is_board_switch));
    DEBUG_PRINT(("--- Global Switch ID is %d ---\n", global_switch_id));
    DEBUG_PRINT(("--- Board Switch ID is %d ---\n", local_switch_id));
    DEBUG_PRINT(("--- Global pos is %s ---\n", get_string_from_array(global_pos, ',').c_str()));
    DEBUG_PRINT(("--- Local pos is %s ---\n", get_string_from_array(local_pos, ',').c_str()));
    DEBUG_PRINT(("--- Unique pos is %s ---\n", get_string_from_array(unique_pos, ',').c_str()));
    DEBUG_PRINT(("--- Fat Tree ID is %s ---\n", get_string_from_array(fat_tree_id, ',').c_str()));
    DEBUG_PRINT(("--- Fat Tree is single router %d ---\n", single_switch_fat_tree));
    DEBUG_PRINT(("--- Fat Tree pos is %s ---\n", get_string_from_array(fat_tree_pos, ',').c_str()));
    DEBUG_PRINT(("--- Fat Tree num links edge<->cores %d ---\n", num_links_pair));
    DEBUG_PRINT(("--- Fat Tree switches level first %d ---\n\n", switches_first_level));
    fflush(stdout);

    // Default width for HxNet is 1. More than 1 not supported currently.
    std::string width = params.find<std::string>("width", "");
    if ( width.compare("") == 0 ) {
        for ( int i = 0 ; i < dimensions ; i++ )
            dim_width[i] = 1;
    } else {
        parseDimString(width, dim_width);
    }

    if (board_shape[0] == 2) {
        is_hx2 = true;
    }

    if (!is_board_switch) {
        if(single_switch_fat_tree){
            this->down_ports_edge = num_ports;
        }else{
            this->down_ports_edge = ceil(num_ports / 2); // for 1:1 topologies
        }
    } else {
         this->down_ports_edge = -1;
    }

    // This is probably not that useful for us, might remove later
    int next_port = 0;
    for ( int d = 0 ; d < dimensions ; d++ ) {
        for ( int i = 0 ; i < 2 ; i++ ) {
            port_start[d][i] = next_port;
            next_port += dim_width[d];
        }
    }

    // This is probably not that useful for us, might remove later
    int needed_ports = 0;
    for ( int i = 0 ; i < dimensions ; i++ ) {
        needed_ports += 2 * dim_width[i];
    }

    local_port_start = num_ports - num_local_ports;// Local delivery is on the last ports

    id_loc = new int[dimensions]; // This is probably not that useful for us, might remove later
    idToLocation(router_id, id_loc); // This is probably not that useful for us, might remove later
    for (int i = 0; i < 4; i++) {
        port_used[i] = 0;
    }
}

topo_hamming::~topo_hamming()
{
    for (int i = 0; i < 4; i++) {
        //printf("Switch %d - Port %d used %d times (%d)\n",global_switch_id, i, port_used[i], local_port_start);
    }
    delete [] id_loc;
    delete [] dim_size;
    delete [] dim_width;
    delete [] port_start;
}

std::vector<int> topo_hamming::getOutputPortFor(int fat_tree_destination) {
    std::vector<int> output_ports;
    if (isBoardSwitch(global_switch_id)) {
        output_ports.push_back(-1);
        return output_ports;
    }

    int switch_id_first_level = fat_tree_destination / down_ports_edge;
    if(is_tree_edge_switch()){        
        if(fat_tree_pos[1] == switch_id_first_level){ // I am the edge destination switch
            output_ports.push_back(fat_tree_destination % down_ports_edge);
        }else{ // I am the edge source switch
            // Push all up ports to core
            int first_port = down_ports_edge;        
            for (int link_idx = first_port; link_idx < num_ports; link_idx++) {
                output_ports.push_back(link_idx);
            }
        }
    }else{        
        int first_port = (switch_id_first_level) * num_links_pair;        
        for (int link_idx = 0; link_idx < num_links_pair; link_idx++) {
            output_ports.push_back(first_port + link_idx);
        }
    }

    DEBUG_PRINT(("[Tree switch (%s) <%d,%d,%d>] Edge %d Fat tree pos %d Switch id first level %d down ports edge %d\n", 
                is_in_row_tree()?"Row":"Col",
                fat_tree_id[1], fat_tree_pos[0], fat_tree_pos[1],
                is_tree_edge_switch(), fat_tree_pos[1], switch_id_first_level, down_ports_edge));

    return output_ports;
}



void
topo_hamming::route_packet(int port, int vc, internal_router_event* ev)
{    
    if(is_board_switch){
        route_packet_mesh(port, vc, ev);
        port_used[ev->getNextPort()] += 1;
        if (global_switch_id == 0 && ev->getEncapsulatedEvent()->getSizeInBits()> 800 && ev->getNextPort() < 4) {
            //printf("Switch %d - Port %d\n",global_switch_id, ev->getNextPort());
        }
    }else{
        route_packet_tree(port, vc, ev);
    }
}

bool 
topo_hamming::wrap_south(uint board_row_dest){
    // Shall I go south even if the destination is north?
    if (routing_algo == "min-adaptive-nogl") { // Never go on global links if src and dst are on the same board
        return false;
    }    
    uint tree_hops;
    if(get_nrows() == 1){
        tree_hops = 1; // If I only have one row, there is a wrap-around link rather than a switch
    }else{
        tree_hops = 2;
    }
    uint board_row_here = unique_pos[0];
    assert(board_row_dest < board_row_here);
    uint distance_south = (get_nrows_board() - board_row_here - 1) + tree_hops + board_row_dest; // Reach the south edge, cross the tree, reach the board
    uint distance_north = board_row_here - board_row_dest;
    return distance_south <= distance_north;
}

bool 
topo_hamming::wrap_north(uint board_row_dest){    
    // Shall I go north even if the destination is south?
    if (routing_algo == "min-adaptive-nogl") { // Never go on global links if src and dst are on the same board
        return false;
    }    
    uint tree_hops;
    if(get_nrows() == 1){
        tree_hops = 1; // If I only have one row, there is a wrap-around link rather than a switch
    }else{
        tree_hops = 2;
    }
    uint board_row_here = unique_pos[0];
    assert(board_row_dest > board_row_here);
    uint distance_south = board_row_dest - board_row_here;
    uint distance_north = board_row_here + tree_hops + (get_nrows_board() - board_row_dest - 1); // Reach the north edge, cross the tree, reach the board
    return distance_north <= distance_south;
}

bool 
topo_hamming::wrap_east(uint board_col_dest){
    // Shall I go east even if the destination is west?
    if (routing_algo == "min-adaptive-nogl") { // Never go on global links if src and dst are on the same board
        return false;
    }    
    uint tree_hops;
    if(get_ncols() == 1){
        tree_hops = 1; // If I only have one col, there is a wrap-around link rather than a switch
    }else{
        tree_hops = 2;
    }
    uint board_col_here = unique_pos[1]; 
    assert(board_col_dest < board_col_here);
    uint distance_west = board_col_here - board_col_dest;
    uint distance_east = (get_ncols_board() - board_col_here - 1) + tree_hops + board_col_dest; // Reach the east edge, cross the tree, reach the board
    return distance_east <= distance_west;
}

bool 
topo_hamming::wrap_west(uint board_col_dest){
    // Shall I go west even if the destination is east?
    if (routing_algo == "min-adaptive-nogl") { // Never go on global links if src and dst are on the same board
        return false;
    }    
    uint tree_hops;
    if(get_ncols() == 1){
        tree_hops = 1; // If I only have one col, there is a wrap-around link rather than a switch
    }else{
        tree_hops = 2;
    }
    uint board_col_here = unique_pos[1]; 
    assert(board_col_dest > board_col_here);
    uint distance_west = board_col_here + tree_hops + (get_ncols_board() - board_col_dest - 1); // Reach the west edge, cross the tree, reach the board
    uint distance_east = board_col_dest - board_col_here;
    return distance_west <= distance_east;
}

void
topo_hamming::route_packet_mesh(int port, int vc, internal_router_event* ev)
{
    
    uint board_row_src = get_coord_board_row(ev->getSrc());
    uint board_col_src = get_coord_board_col(ev->getSrc());
    uint row_src = get_coord_row(ev->getSrc());
    uint col_src = get_coord_col(ev->getSrc());
    uint board_row_dest = get_coord_board_row(ev->getDest());
    uint board_col_dest = get_coord_board_col(ev->getDest());
    uint row_dest = get_coord_row(ev->getDest());
    uint col_dest = get_coord_col(ev->getDest());
    uint board_row_here = unique_pos[0];
    uint board_col_here = unique_pos[1]; 
    uint row_here = unique_pos[2]; 
    uint col_here = unique_pos[3]; 
    std::vector<char> candidate_ports; // N, S, E, W    
    
    DEBUG_PRINT(("[Mesh switch <%d,%d,%d,%d>] Routing packet from <%d,%d,%d,%d> to <%d,%d,%d,%d>\n", 
            row_here, col_here, board_row_here, board_col_here, 
            row_src, col_src, board_row_src, board_col_src, 
            row_dest, col_dest, board_row_dest, board_col_dest));

    if(row_src == row_here && col_src == col_here && board_row_src == board_row_here && board_col_src == board_col_here){
        DEBUG_PRINT(("[Mesh switch <%d,%d,%d,%d>] Packet received from NIC\n", 
                     row_here, col_here, board_row_here, board_col_here));
        if(port != port_mnemonic_to_id('C')){
            throw std::runtime_error("Received a packet from NIC but not from the correct port.\n");
        }
    }

    // Destination on this board
    if(row_dest == row_here && col_dest == col_here){
        if(board_row_dest > board_row_here || 
           (board_row_dest < board_row_here && wrap_south(board_row_dest))){
            candidate_ports.push_back('S');
        }
        if(board_row_dest < board_row_here || 
           (board_row_dest > board_row_here && wrap_north(board_row_dest))){
            candidate_ports.push_back('N');
        }

        if(board_col_dest > board_col_here || 
           (board_col_dest < board_col_here && wrap_east(board_col_dest))){
            candidate_ports.push_back('E');
        }
        if(board_col_dest < board_col_here || 
           (board_col_dest > board_col_here && wrap_west(board_col_dest))){
            candidate_ports.push_back('W');
        }
    }
    // Destination on a different board but on the same column
    else if(row_dest != row_here && col_dest == col_here){
        uint dist_n = board_row_here;
        uint dist_s = get_nrows_board() - board_row_here - 1;        
        uint min_dist = std::min(dist_n, dist_s);
        if(dist_n == min_dist){
            candidate_ports.push_back('N');
        }
        if(dist_s == min_dist){
            candidate_ports.push_back('S');
        }

        if(board_col_dest > board_col_here){
            candidate_ports.push_back('E');
        }else if(board_col_dest < board_col_here){
            candidate_ports.push_back('W');
        }
    }
    // Destination on a different board but on the same row
    else if(row_dest == row_here && col_dest != col_here){
        uint dist_w = board_col_here;
        uint dist_e = get_ncols_board() - board_col_here - 1;
        uint min_dist = std::min(dist_w, dist_e);
        if(dist_w == min_dist){
            candidate_ports.push_back('W');
        }
        if(dist_e == min_dist){
            candidate_ports.push_back('E');
        }

        if(board_row_dest > board_row_here){
            candidate_ports.push_back('S');
        }else if(board_row_dest < board_row_here){
            candidate_ports.push_back('N');
        }
    }
    // Destination on a differerent board on a different row and column
    else{
        // Compute distance from all the edges
        uint dist_n, dist_e, dist_s, dist_w;
        dist_n = board_row_here;
        dist_e = get_ncols_board() - board_col_here - 1;
        dist_s = get_nrows_board() - board_row_here - 1;
        dist_w = board_col_here;
        uint min_dist = std::min(std::min(dist_s, dist_n), std::min(dist_e, dist_w));
        if(dist_n == min_dist){
            candidate_ports.push_back('N');
        }
        if(dist_e == min_dist){
            candidate_ports.push_back('E');
        }
        if(dist_s == min_dist){
            candidate_ports.push_back('S');
        }
        if(dist_w == min_dist){
            candidate_ports.push_back('W');
        }
    }
    DEBUG_PRINT(("[Mesh switch <%d,%d,%d,%d>] Candidate ports %s\n", 
                row_here, col_here, board_row_here, board_col_here, 
                get_string_from_array(candidate_ports, ',').c_str()));
    // At this point, candidate_ports contains the ports the bring the packet closer to the destination.
    // To implement North-Last routing, thus avoiding deadlocks, if we have multiple
    // directions in 'candidate_ports' (some of which are E/W), we should remove north. 
    // Basically, when we are on a different col than that of the dest, north can only 
    // be taken if it is the only destination in candidate_ports
    if(candidate_ports.size() > 1 && board_col_here != board_col_dest){
        // Remove N from candidate_ports
        candidate_ports.erase(std::remove(candidate_ports.begin(), candidate_ports.end(), 'N'), candidate_ports.end()); 
    }

    // Convert mnemonic port IDs to real IDs
    std::vector<uint> candidate_ports_real;
    std::vector<uint> candidate_vcs;
    for(auto c : candidate_ports){
        candidate_ports_real.push_back(port_mnemonic_to_id(c));
        if(c == 'N'){
            // If I am on the north edge and I have to go further north (i.e., on the tree)
            if(board_row_here == 0){
                candidate_vcs.push_back(vc + 1);
            }else{
                candidate_vcs.push_back(vc);
            }
        }else if(c == 'E'){
            // If I am on the east edge and I have to go further east (i.e., on the tree)
            if(board_col_here == get_ncols_board() - 1){
                candidate_vcs.push_back(vc + 1);
            }else{
                candidate_vcs.push_back(vc);
            }
        }else if(c == 'S'){
            // If I am on the south edge and I have to go further south (i.e., on the tree)
            if(board_row_here == get_ncols_board() - 1){
                candidate_vcs.push_back(vc + 1);
            }else{
                candidate_vcs.push_back(vc);
            }
        }else if(c == 'W'){
            // If I am on the west edge and I have to go further west (i.e., on the tree)
            if(board_col_here == 0){
                candidate_vcs.push_back(vc + 1);
            }else{
                candidate_vcs.push_back(vc);
            }
        }else{
            throw std::runtime_error("Unknown port ID " + c);
        }
    }

    // If the destination is on this switch
    if(candidate_ports_real.empty()){
        DEBUG_PRINT(("[Mesh switch <%d,%d,%d,%d>] Routing packet to NIC\n", row_here, col_here, board_row_here, board_col_here));
        ev->setNextPort(port_mnemonic_to_id('C')); // To NIC
    }else{
        int max_credits = -1;
        int selected_port, selected_vc;
        for(size_t i = 0; i < candidate_ports_real.size(); i++){
            int next_port = candidate_ports_real[i];
            int next_vc = candidate_vcs[i];
            int credits = output_credits[next_port * num_vcs + next_vc];
            //printf("Rtr %d - Credits in port %d (%d) -> %d\n", router_id, next_port, candidate_ports_real.size(), credits);
            if(credits > max_credits){
                selected_port = next_port;
                selected_vc = next_vc;
                max_credits = credits;
            }
        }
        //printf("Rtr %d - Port Selected is %d\n", router_id, selected_port);
        DEBUG_PRINT(("[Mesh switch <%d,%d,%d,%d>] Routing packet on port %c VC %d\n", 
                    row_here, col_here, board_row_here, board_col_here, 
                    port_id_to_mnemonic(selected_port), selected_vc));
        ev->setNextPort(selected_port);
        ev->setVC(selected_vc);
    }    
}

void
topo_hamming::route_packet_tree(int port, int vc, internal_router_event* ev)
{
    uint board_row_src = get_coord_board_row(ev->getSrc());
    uint board_col_src = get_coord_board_col(ev->getSrc());    
    uint row_src = get_coord_row(ev->getSrc());    
    uint col_src = get_coord_col(ev->getSrc());    
    uint board_row_dest = get_coord_board_row(ev->getDest());
    uint board_col_dest = get_coord_board_col(ev->getDest());    
    uint row_dest = get_coord_row(ev->getDest());
    uint col_dest = get_coord_col(ev->getDest());

    std::vector<int> dests_in_tree;
    if(is_in_row_tree()){
        uint dest_in_tree_w = col_dest*2;     // Entering the mesh from West
        uint dest_in_tree_e = col_dest*2 + 1; // Entering the mesh from East
        uint distance_w = board_col_dest; // Distance of the destination from the West border
        uint distance_e = get_ncols_board() - board_col_dest - 1; // Distance of the destination from the East border
        uint min_distance = std::min(distance_w, distance_e);
        if(distance_w == min_distance){
            dests_in_tree.push_back(dest_in_tree_w);
        }
        if(distance_e == min_distance){
            dests_in_tree.push_back(dest_in_tree_e);
        }
    }else if(is_in_col_tree()){
        uint dest_in_tree_n = row_dest*2;     // Entering the mesh from North
        uint dest_in_tree_s = row_dest*2 + 1; // Entering the mesh from South
        uint distance_n = board_row_dest; // Distance of the destination from the North border
        uint distance_s = get_nrows_board() - board_row_dest - 1; // Distance of the destination from the South border
        uint min_distance = std::min(distance_n, distance_s);
        if(distance_n == min_distance){
            dests_in_tree.push_back(dest_in_tree_n);
        }
        if(distance_s == min_distance){
            dests_in_tree.push_back(dest_in_tree_s);
        }
    }
    
    std::set<int> out_ports_s;
    for(auto dest_in_tree : dests_in_tree){
        for(auto p : getOutputPortFor(dest_in_tree)){
            out_ports_s.insert(p);
        }
    }
    std::vector<int> out_ports;
    for(auto p : out_ports_s){
        out_ports.push_back(p);
    }

    int max_credits = -1;
    int selected_port;
    for(size_t i = 0; i < out_ports.size(); i++){
        int next_port = out_ports[i];
        int credits = output_credits[next_port * num_vcs + vc];
        if(credits > max_credits){
            selected_port = next_port;
            max_credits = credits;
        }
    }
    ev->setNextPort(selected_port);
    ev->setVC(vc);

    DEBUG_PRINT(("[Tree switch (%s) <%d,%d,%d>] Routing packet from <%d,%d,%d,%d> to <%d,%d,%d,%d>. Dests %s. Port %d to ports %s. Selected %d\n", 
                 is_in_row_tree()?"Row":"Col",
                 fat_tree_id[1], fat_tree_pos[0], fat_tree_pos[1],
                 row_src, col_src, board_row_src, board_col_src,
                 row_dest, col_dest, board_row_dest, board_col_dest,
                 get_string_from_array(dests_in_tree, ',').c_str(),
                 port, get_string_from_array(out_ports, ',').c_str(),
                 selected_port));
}

// Need to double check this and its overall role
internal_router_event*
topo_hamming::process_input(RtrEvent* ev)
{
    topo_hamming_event* tt_ev = new topo_hamming_event(dimensions);
    tt_ev->setEncapsulatedEvent(ev);
    tt_ev->setVC(tt_ev->getVN() * 2);
    
    // Need to figure out what the mesh address is for easier
    // routing.
    int run_id = get_dest_router(tt_ev->getDest());
    idToLocation(run_id, tt_ev->dest_loc);

	return tt_ev;
}


void topo_hamming::routeInitData(int port, internal_router_event* ev, std::vector<int> &outPorts)
{
    DEBUG_PRINT(("Routing init data\n")); fflush(stdout);
    topo_hamming_init_event *tt_ev = static_cast<topo_hamming_init_event*>(ev);
    if ( tt_ev->phase == 0 ) {
        if ( (0 == router_id) && (ev->getDest() == INIT_BROADCAST_ADDR) ) {
            /* Broadcast has arrived at 0.  Switch Phases */
            tt_ev->phase = 1;
        } else {
            route_packet(port, 0, ev);
            outPorts.push_back(ev->getNextPort());
            return;
        }
    }

    /*
     * Find dimension came in on
     * Send in positive direction in all dimensions that level and higher (unless at end)
     */
    int inc_dim = 0;
    if ( tt_ev->phase == 2 ) {
        for ( ; inc_dim < dimensions ; inc_dim++ ) {
            if ( port == port_start[inc_dim][1] ) {
                break;
            }
        }
    }

    tt_ev->phase = 2;

    for ( int dim = inc_dim ; dim < dimensions ; dim++ ) {
        if ( (id_loc[dim] + 1) < dim_size[dim] ) {
            outPorts.push_back(port_start[dim][0]);
        }
    }

    // Also, send to hosts
    for ( int p = 0 ; p < num_local_ports ; p++ ) {
        if ( (local_port_start + p) != port ) {
            outPorts.push_back(local_port_start +p);
        }
    }
    
}


internal_router_event* topo_hamming::process_InitData_input(RtrEvent* ev)
{
    DEBUG_PRINT(("Process init")); fflush(stdout);
    topo_hamming_init_event* tt_ev = new topo_hamming_init_event(dimensions);
    tt_ev->setEncapsulatedEvent(ev);
    if ( tt_ev->getDest() == INIT_BROADCAST_ADDR ) {
        /* For broadcast, first send to rtr 0 */
        idToLocation(0, tt_ev->dest_loc);
    } else {
        int rtr_id = get_dest_router(tt_ev->getDest());
        idToLocation(rtr_id, tt_ev->dest_loc);
    }
    return tt_ev;
}

bool topo_hamming::isBoardSwitch(int switch_id) { 
    int switches_per_board = this->board_shape[0] * this->board_shape[1] * this->global_shape[0] * this->global_shape[1];
    return switch_id < switches_per_board;
}


std::vector<int> topo_hamming::getUniqueBoardPosFromID(int switch_id) { // Switch ID == Dest ID
    if (!isBoardSwitch(switch_id)) {
        std::vector<int> v = {-1, -1, -1, -1};
        return v;
    }
    
    int row_local = get_coord_board_row(switch_id);
    int col_local = get_coord_board_col(switch_id);
    int row_board = get_coord_row(switch_id);
    int col_board = get_coord_col(switch_id);

    std::vector<int> v = {row_local, col_local, row_board, col_board};
    return v;
}

Topology::PortState
topo_hamming::getPortState(int port) const
{
    // If router is board switch then we need to keep into consideration the single NIC, otherwise it's always R2R
    if (this->is_board_switch) {
        if (port <= 3) {
            return R2R;
        } else if (port == 4) {
            return R2N;
        } else {
            throw std::runtime_error("Board should have at most 5 ports.");
        }
    } else {
        return R2R;
    }
}

// rtr_id is a router id
// For now I have left this one but I don't think it should really ever be used in our case, it is more something from
// the original mesh sst thing.
void
topo_hamming::idToLocation(int run_id, int *location) const
{
	for ( int i = dimensions - 1; i > 0; i-- ) {
		int div = 1;
		for ( int j = 0; j < i; j++ ) {
			div *= dim_size[j];
		}
		int value = (run_id / div);
		location[i] = value;
		run_id -= (value * div);
	}
	location[0] = run_id;
}

void
topo_hamming::parseDimString(const std::string &shape, int *output) const
{
    size_t start = 0;
    size_t end = 0;
    for ( int i = 0; i < dimensions; i++ ) {
        end = shape.find('x',start);
        size_t length = end - start;
        std::string sub = shape.substr(start,length);
        output[i] = strtol(sub.c_str(), NULL, 0);
        start = end + 1;
    }
}

std::vector<int>
topo_hamming::genericShapeParser(const std::string &shape,char symbol, int items) const
{
    size_t start = 0;
    size_t end = 0;
    std::vector<int> out;
    for ( int i = 0; i < items; i++ ) {
        end = shape.find(symbol, start);
        size_t length = end - start;
        std::string sub = shape.substr(start,length);
        out.push_back(strtol(sub.c_str(), NULL, 0));
        start = end + 1;
    }
    return out;
}


// dest_id is a host id
// We shouldn't need this one for the real routing
int
topo_hamming::get_dest_router(int dest_id) const
{
    // This is just a debugging hack, since fat tree routers have 0 local ports, for debugging we set it to 1. Change this
    if (num_local_ports == 0) {
        return 1;
    }

    return dest_id / num_local_ports;
}

// dest_id is a host id
int
topo_hamming::get_dest_local_port(int dest_id) const
{
    return local_port_start + (dest_id % num_local_ports);
}


int
topo_hamming::choose_multipath(int start_port, int num_ports, int dest_dist)
{
    if ( num_ports == 1 ) {
        return start_port;
    } else {
        return start_port + (dest_dist % num_ports);
    }
}

uint 
topo_hamming::get_coord_board(uint addr)
{
    return addr / (board_shape[0]*board_shape[1]);
}

uint
topo_hamming::get_coord_within_board(uint addr)
{
    return addr % (board_shape[0]*board_shape[1]);
}

uint 
topo_hamming::get_coord_board_col(uint addr)
{
    return get_coord_within_board(addr) % board_shape[1];
}

uint 
topo_hamming::get_coord_board_row(uint addr)
{
    return get_coord_within_board(addr) / board_shape[1];
}

uint 
topo_hamming::get_coord_col(uint addr)
{
    return get_coord_board(addr) % global_shape[1];
}

uint 
topo_hamming::get_coord_row(uint addr)
{
    return get_coord_board(addr) / global_shape[1];
}

int
topo_hamming::getEndpointID(int port)
{
    if ( !isHostPort(port) ) return -1;
    return (router_id * num_local_ports) + (port - local_port_start);
}

void
topo_hamming::setOutputBufferCreditArray(int const* array, int vcs)
{
    output_credits = array;
    num_vcs = vcs;
}

void
topo_hamming::setOutputQueueLengthsArray(int const* array, int vcs)
{
    output_queue_lengths = array;
    num_vcs = vcs;
}
