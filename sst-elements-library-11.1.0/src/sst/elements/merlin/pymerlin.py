#!/usr/bin/env python
#
# Copyright 2009-2021 NTESS. Under the terms
# of Contract DE-NA0003525 with NTESS, the U.S.
# Government retains certain rights in this software.
#
# Copyright (c) 2009-2021, NTESS
# All rights reserved.
#
# Portions are copyright of other developers:
# See the file CONTRIBUTORS.TXT in the top level directory
# the distribution for more information.
#
# This file is part of the SST software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

import sys
import sst
import math


# This just suppresses the warning when using emberload.py
if "USING_EMBER_LOAD" in globals():
    print("#WARNING: sst.merlin python module is deprecated and will be removed in SST 12.  Please use the new merlin python modules.")

try:
    input = raw_input
except NameError:
    pass

class Params(dict):
    def __missing__(self, key):
        print("Please enter %s: "%key)
        val = input()
        self[key] = val
        return val
    def subset(self, keys, optKeys = []):
        ret = dict((k, self[k]) for k in keys)
        #ret.update(dict((k, self[k]) for k in (optKeys and self)))
        for k in optKeys:
            if k in self:
                ret[k] = self[k]
        return ret
    def subsetWithRename(self, keys):
        ret = dict()
        for k,nk in keys:
            if k in self:
                ret[nk] = self[k]
        return ret
    # Needed to avoid asking for input when a key isn't present
#    def optional_subset(self, keys):
#        return

_params = Params()
debug = 0

class Topo(object):
    def __init__(self):
        self.topoKeys = []
        self.topoOptKeys = []
        self.bundleEndpoints = True
        def epFunc(epID):
            return None
        self._getEndPoint = epFunc
    def keepEndPointsWithRouter(self):
        self.bundleEndpoints = False
    def getName(self):
        return "NoName"
    def prepParams(self):
        pass
    def setEndPoint(self, endPoint):
        def epFunc(epID):
            return endPoint
        self._getEndPoint = epFunc
    def setEndPointFunc(self, epFunc):
        self._getEndPoint = epFunc
    def build(self):
        pass
    def getRouterNameForId(self,rtr_id):
        return "rtr.%d"%rtr_id
    def findRouterById(self,rtr_id):
        return sst.findComponentByName(self.getRouterNameForId(rtr_id))
    def _instanceRouter(self,rtr_id,rtr_type):
        return sst.Component(self.getRouterNameForId(rtr_id),rtr_type)


class topoSimple(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys.extend(["topology", "debug", "num_ports", "flit_size", "link_bw", "xbar_bw","input_latency","output_latency","input_buf_size","output_buf_size"])
        self.topoOptKeys.extend(["xbar_arb","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"])
    def getName(self):
        return "Simple"
    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        _params["topology"] = "merlin.singlerouter"
        _params["debug"] = debug
        _params["num_ports"] = int(_params["router_radix"])
        _params["num_peers"] = int(_params["router_radix"])

    def build(self):
        rtr = self._instanceRouter(0,"merlin.hr)_router")
        rtr.setSubComponent("topology","merlin.singlerouter",0)
        _params["topology"] = "merlin.singlerouter"
        _params["debug"] = debug
#        rtr.addParams(_params.subset(self.rtrKeys))
        rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
        rtr.addParam("id", 0)

        for l in range(_params["num_ports"]):
            ep = self._getEndPoint(l).build(l, {})
            if ep:
                link = sst.Link("link:%d"%l)
                if self.bundleEndpoints:
                    link.setNoCut()
                link.connect(ep, (rtr, "port%d"%l, _params["link_lat"]) )

    def getRouterNameForId(self,rtr_id):
        return "router"
                

class topoTorus(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys.extend(["topology", "debug", "num_ports", "flit_size", "link_bw", "nic_link_bw", "xbar_bw", "torus:shape", "torus:width", "torus:local_ports","input_latency","output_latency","input_buf_size","output_buf_size"])
        self.topoOptKeys.extend(["xbar_arb","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"])
    def getName(self):
        return "Torus"
    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        peers = 1
        radix = 0
        self.dims = []
        self.dimwidths = []
        if not "torus:shape" in _params:
            self.nd = int(_params["num_dims"])
            for x in range(self.nd):
                print("Dim %d size:"%x)
                ds = int(input())
                self.dims.append(ds)
            _params["torus:shape"] = self._formatShape(self.dims)
        else:
            self.dims = [int(x) for x in _params["torus:shape"].split('x')]
            self.nd = len(self.dims)
        if not "torus:width" in _params:
            for x in range(self.nd):
                print("Dim %d width (# of links in this dimension):" % x)
                dw = int(input())
                self.dimwidths.append(dw)
            _params["torus:width"] = self._formatShape(self.dimwidths)
        else:
            self.dimwidths = [int(x) for x in _params["torus:width"].split('x')]

        local_ports = int(_params["torus:local_ports"])
        radix = local_ports + 2 * sum(self.dimwidths)

        #print("Radix Torus is " + str(radix))

        for x in self.dims:
            peers = peers * x
        peers = peers * local_ports

        #print("Peer is " + str(peers))

        _params["num_peers"] = peers
        _params["num_dims"] = self.nd
        _params["topology"] = _params["topology"] = "merlin.torus"
        _params["debug"] = debug
        _params["num_ports"] = _params["router_radix"] = radix
        _params["torus:local_ports"] = local_ports

    def _formatShape(self, arr):
        return 'x'.join([str(x) for x in arr])


    def _idToLoc(self,rtr_id):
        foo = list()
        for i in range(self.nd-1, 0, -1):
            div = 1
            for j in range(0, i):
                div = div * self.dims[j]
            value = (rtr_id // div)
            foo.append(value)
            rtr_id = rtr_id - (value * div)
        foo.append(rtr_id)
        foo.reverse()
        return foo

    def getRouterNameForId(self,rtr_id):
        return self.getRouterNameForLocation(self._idToLoc(rtr_id))

    def getRouterNameForLocation(self,location):
        return "rtr.%s"%(self._formatShape(location))
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location))
    

    def build(self):    
        num_routers = _params["num_peers"] // _params["torus:local_ports"]
        links = dict()
        def getLink(leftName, rightName, num):
            name = "link.%s:%s:%d"%(leftName, rightName, num)
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]

        swap_keys = [("torus:shape","shape"),("torus:width","width"),("torus:local_ports","local_ports")]

        _topo_params = _params.subsetWithRename(swap_keys);

        for i in range(num_routers):
            # set up 'mydims'
            mydims = self._idToLoc(i)
            mylocstr = self._formatShape(mydims)
            rtr = self._instanceRouter(i,"merlin.hr_router")
            #rtr = sst.Component("rtr.%s"%mylocstr, "merlin.hr_router")
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", i)
            topology = rtr.setSubComponent("topology","merlin.torus")
            topology.addParams(_topo_params)

            port = 0
            for dim in range(self.nd):
                theirdims = mydims[:]

                # Positive direction
                theirdims[dim] = (mydims[dim] +1 ) % self.dims[dim]
                theirlocstr = self._formatShape(theirdims)
                for num in range(self.dimwidths[dim]):
                    rtr.addLink(getLink(mylocstr, theirlocstr, num), "port%d"%port, _params["link_lat"])
                    port = port+1

                # Negative direction
                theirdims[dim] = ((mydims[dim] -1) +self.dims[dim]) % self.dims[dim]
                theirlocstr = self._formatShape(theirdims)
                for num in range(self.dimwidths[dim]):
                    rtr.addLink(getLink(theirlocstr, mylocstr, num), "port%d"%port, _params["link_lat"])
                    port = port+1

            for n in range(_params["torus:local_ports"]):
                nodeID = int(_params["torus:local_ports"]) * i + n
                ep = self._getEndPoint(nodeID).build(nodeID, {})
                if ep:
                    nicLink = sst.Link("nic.%d:%d"%(i, n))
                    if self.bundleEndpoints:
                       nicLink.setNoCut()
                    nicLink.connect(ep, (rtr, "port%d"%port, _params["link_lat"]))
                port = port+1



class topoMesh(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys = ["topology", "debug", "num_ports", "flit_size", "link_bw", "xbar_bw", "mesh:shape", "mesh:width", "mesh:local_ports","input_latency","output_latency","input_buf_size","output_buf_size"]
        self.topoOptKeys = ["xbar_arb","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"]
    def getName(self):
        return "Mesh"
    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        peers = 1
        radix = 0
        self.dims = []
        self.dimwidths = []
        if not "mesh:shape" in _params:
            self.nd = int(_params["num_dims"])
            for x in range(self.nd):
                print("Dim %d size:"%x)
                ds = int(input())
                self.dims.append(ds);
            _params["mesh:shape"] = self._formatShape(self.dims)
        else:
            self.dims = [int(x) for x in _params["mesh:shape"].split('x')]
            self.nd = len(self.dims)
        if not "mesh:width" in _params:
            for x in range(self.nd):
                print("Dim %d width (# of links in this dimension):" % x)
                dw = int(input())
                self.dimwidths.append(dw)
            _params["mesh:width"] = self._formatShape(self.dimwidths)
        else:
            self.dimwidths = [int(x) for x in _params["mesh:width"].split('x')]

        local_ports = int(_params["mesh:local_ports"])
        radix = local_ports + 2 * sum(self.dimwidths)

        for x in self.dims:
            peers = peers * x
        peers = peers * local_ports

        _params["num_peers"] = peers
        _params["num_dims"] = self.nd
        _params["topology"] = _params["topology"] = "merlin.mesh"
        _params["debug"] = debug
        _params["num_ports"] = _params["router_radix"] = radix
        _params["mesh:local_ports"] = local_ports

    def _formatShape(self, arr):
        return 'x'.join([str(x) for x in arr])


    def _idToLoc(self,rtr_id):
        foo = list()
        for i in range(self.nd-1, 0, -1):
            div = 1
            for j in range(0, i):
                div = div * self.dims[j]
            value = (rtr_id // div)
            foo.append(value)
            rtr_id = rtr_id - (value * div)
        foo.append(rtr_id)
        foo.reverse()
        return foo

    def getRouterNameForId(self,rtr_id):
        return self.getRouterNameForLocation(self._idToLoc(rtr_id))

    def getRouterNameForLocation(self,location):
        return "rtr.%s"%(self._formatShape(location))
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location));
    
    
    def build(self):

        num_routers = _params["num_peers"] // _params["mesh:local_ports"]
        links = dict()
        def getLink(leftName, rightName, num):
            name = "link.%s:%s:%d"%(leftName, rightName, num)
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]

        swap_keys = [("mesh:shape","shape"),("mesh:width","width"),("mesh:local_ports","local_ports")]

        _topo_params = _params.subsetWithRename(swap_keys);

        for i in range(num_routers):
            # set up 'mydims'
            mydims = self._idToLoc(i)
            mylocstr = self._formatShape(mydims)

            rtr = self._instanceRouter(i,"merlin.hr_router")
            #rtr = sst.Component("rtr.%s"%mylocstr, "merlin.hr_router")
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", i)
            topology = rtr.setSubComponent("topology","merlin.mesh")
            topology.addParams(_topo_params)

            port = 0
            for dim in range(self.nd):
                theirdims = mydims[:]

                # Positive direction
                if mydims[dim]+1 < self.dims[dim]:
                    theirdims[dim] = (mydims[dim] +1 ) % self.dims[dim]
                    theirlocstr = self._formatShape(theirdims)
                    for num in range(self.dimwidths[dim]):
                        rtr.addLink(getLink(mylocstr, theirlocstr, num), "port%d"%port, _params["link_lat"])
                        port = port+1
                else:
                    port += self.dimwidths[dim]

                # Negative direction
                if mydims[dim] > 0:
                    theirdims[dim] = ((mydims[dim] -1) +self.dims[dim]) % self.dims[dim]
                    theirlocstr = self._formatShape(theirdims)
                    for num in range(self.dimwidths[dim]):
                        rtr.addLink(getLink(theirlocstr, mylocstr, num), "port%d"%port, _params["link_lat"])
                        port = port+1
                else:
                    port += self.dimwidths[dim]

            for n in range(_params["mesh:local_ports"]):
                nodeID = int(_params["mesh:local_ports"]) * i + n
                ep = self._getEndPoint(nodeID).build(nodeID, {})
                if ep:
                    nicLink = sst.Link("nic.%d:%d"%(i, n))
                    if self.bundleEndpoints:
                       nicLink.setNoCut()
                    nicLink.connect(ep, (rtr, "port%d"%port, _params["link_lat"]))
                port = port+1


class topoHamming(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys = ["topology", "debug", "num_ports", "flit_size", "link_bw",  "nic_link_bw", "xbar_bw", "hamming:fat_tree_shape", "hamming:shape", "hamming:single_switch_fat_tree", "hamming:link_width","hamming:switches_first_level", "hamming:board_shape", "hamming:global_shape", "hamming:is_board_switch", "hamming:algorithm", "hamming:global_switch_id", "hamming:local_switch_id", "hamming:global_pos", "hamming:local_pos", "hamming:unique_pos", "hamming:fat_tree_id", "hamming:fat_tree_pos", "hamming:width", "hamming:local_ports","input_latency","output_latency","input_buf_size","output_buf_size"]
        self.topoOptKeys = ["xbar_arb","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"]
    def getName(self):
        return "Hamming"
    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        peers = 1
        radix = 0
        self.num_boards = 1
        self.switch_per_board = 1
        self.dims = []
        self.global_shape = []
        self.dimwidths = []
        self.list_routers = {}
        self.global_to_local = {}
        self.global_router_id = 0
        if not "hamming:shape" in _params:
            self.nd = int(_params["num_dims"])
            for x in range(self.nd):
                #print("Dim %d size:"%x)
                ds = int(input())
                self.dims.append(ds)
            _params["hamming:shape"] = self._formatShape(self.dims)
        else: # If we define shape when running SST
            self.dims = [int(x) for x in _params["hamming:shape"].split('x')]
            self.nd = len(self.dims)
            self.global_shape = [int(x) for x in _params["hamming:global_shape"].split('x')]
            self.fat_tree_shape = _params["hamming:fat_tree_shape"].split(",")
            self.radix_fat_tree_switches = int(self.fat_tree_shape[1])
            self.fat_tree_shape = [int(x) for x in self.fat_tree_shape[0].split(':')]
        if not "hamming:width" in _params:
            for x in range(self.nd):
                #print("Dim %d width (# of links in this dimension):" % x)
                dw = int(input())
                self.dimwidths.append(dw)
            _params["hamming:width"] = self._formatShape(self.dimwidths)
            #print("default width is " + str(_params["hamming:width"]))
        else:
            self.dimwidths = [int(x) for x in _params["hamming:width"].split('x')]
            #print("set width is " + str(_params["hamming:width"]))

        local_ports = int(_params["hamming:local_ports"])
        # We should only need 1x1 width, but let's keep it open
        radix = local_ports + 2 * sum(self.dimwidths)

        '''print("self.dimwidths" + str(self.dimwidths))
        print("self.dims" + str(self.dims))
        print("self.global" + str(self.global_shape))
        print("self.fat_tree_shape" + str(self.fat_tree_shape))
        print("radix is " + str(radix))'''
        # Board shape
        for x in self.dims:
            peers = peers * x
            self.switch_per_board = self.switch_per_board * x
        # Global Shape
        for x in self.global_shape:
            peers = peers * x   
        peers = peers * local_ports
        self.num_boards = int(peers /  self.switch_per_board)
        self.switches_first_level =  -1
        self.link_width = -1 

        #print("Peers is " + str(peers))
        #print("num_boards is " + str(self.num_boards))
        #print("switch_per_board is " + str(self.switch_per_board))

        _params["num_peers"] = peers
        _params["num_dims"] = self.nd
        _params["topology"] = _params["topology"] = "merlin.hamming"
        _params["debug"] = debug
        _params["num_ports"] = _params["router_radix"] = radix
        _params["hamming:local_ports"] = local_ports

    def _formatShape(self, arr):
        return 'x'.join([str(x) for x in arr])

    def _idToLoc(self,rtr_id):
        foo = list()
        for i in range(self.nd-1, 0, -1):
            div = 1
            for j in range(0, i):
                div = div * self.dims[j]
            value = (rtr_id // div)
            foo.append(value)
            rtr_id = rtr_id - (value * div)
        foo.append(rtr_id)
        foo.reverse()
        return foo


    def getRouterNameForId(self,rtr_id):
        return self.getRouterNameForLocation(self._idToLoc(rtr_id))

    def getRouterNameForLocation(self,location):
        return "rtr.%d"%(self.global_router_id)
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location))

    def getGlobalDims(self,location):
        maxRowLength = self.dims[0] * self.global_shape[0]

        myRow = location // (maxRowLength - 1)
        myCol = location % (maxRowLength - 1)

        myId = [myRow, myCol]
        return myId

    def getLocalDims(self, location):
        myRow = location // (self.dims[1])
        myCol = location % (self.dims[1])
        myId = [myRow, myCol]
        return myId

    def getUniquePos(self, local_pos, board_id):
        myRow = board_id // (self.global_shape[1])
        myCol = board_id % (self.global_shape[1])
        myId = [myRow, myCol]
        unique_pos = [local_pos, myId]
        return unique_pos

    def getRouterNameString(self, pos):
        return "{}x{}x{}x{}".format(pos[0][0], pos[0][1], pos[1][0], pos[1][1])
        
    def getTotRoutersMeshes(self):
        tot_routers = 1
        for d in self.global_shape:
            tot_routers = tot_routers * d
        for d in self.dims:
            tot_routers = tot_routers * d
        #print("Tot Routers -> {}".format(tot_routers))
        return tot_routers

    def getUniquePosFromGlobal(self, global_pos):
        total_rows = self.global_shape[0] * self.dims[0]
        total_cols = self.global_shape[1] * self.dims[1]
        unique_id = (global_pos[0] * total_cols) + global_pos[1]
        #print("Unique ID is {}".format(unique_id))

    def GlobalToString(self, glob):
        return "{}x{}".format(glob[0], glob[1])


    def getOffsetPerDirection(self, direction):
        if (direction == 0):
            return [-1, 0]
        elif (direction == 1):
            return [0, 1]
        elif (direction == 2):
            return [1, 0]
        elif (direction == 3):
            return [0, -1]

    def isInsideBoard(self, pos):
        return ((pos[0] >= 0 and pos[0] < self.dims[0]) and (pos[1] >= 0 and pos[1] < self.dims[1]))

    def isFirstOrLast(self, pos, limit_direction):
        my_pos = pos % limit_direction
        return (my_pos == 0 or my_pos == limit_direction - 1)

    def isFirst(self, pos, limit_direction):
        my_pos = pos % limit_direction
        return (my_pos == 0)

    def setParameters(self, _params, is_bd_sw, global_router_id, local_router_id, my_glob_id, my_loc_id, unique_pos, fat_tree_id, fat_tree_pos):
        _params["hamming:is_board_switch"] = is_bd_sw
        _params["hamming:global_switch_id"] = global_router_id
        _params["hamming:local_switch_id"] = local_router_id
        _params["hamming:global_pos"] = my_glob_id
        _params["hamming:local_pos"] = my_loc_id
        _params["hamming:unique_pos"] = [unique_pos[0][0], unique_pos[0][1], unique_pos[1][0], unique_pos[1][1]]
        _params["hamming:fat_tree_id"] = fat_tree_id
        _params["hamming:fat_tree_pos"] = fat_tree_pos
        _params["hamming:link_width"] = self.link_width
        _params["hamming:switches_first_level"] = self.switches_first_level
        _params["hamming:single_switch_fat_tree"] = False

    def createRowFatTree(self, nodes, total, row, col):

        # Helper function
        links = dict()
        def getLink(leftName, rightName, num):
            name = "link.%s:%s:%d"%(leftName, rightName, num)
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]

        # Determine if we are creating row or col wise fat Tree
        type_tree = ""
        if (row == -1):
            type_tree = "col"
        else:
            type_tree = "row"
        
        # Case 1:1, initialize overall values
        num_ports_per_switch = self.radix_fat_tree_switches
        num_switches_first_level =  int(math.ceil(nodes / (num_ports_per_switch / 2)))
        tot_ports_up = num_switches_first_level * (num_ports_per_switch / 2)
        num_swithes_second_level = int(math.ceil(tot_ports_up / num_ports_per_switch))
        down_ports = (num_ports_per_switch / 2)
        list_routers_first_level = {}
        list_routers_second_level = {}
        #print("Switches Level 1 {}, Tot Up ports {} - Switches Level 2 {}".format(num_switches_first_level, tot_ports_up, num_swithes_second_level))
        
        # Iterate routers first level and link them to board switches
        col_idx = 0
        for router_level_1 in range(int(num_switches_first_level)):
            # Create router
            current_down_port = down_ports
            port = 0
            rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = self.radix_fat_tree_switches
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], [0,row], [0, router_level_1])
            _params["hamming:link_width"] = int(down_ports / num_swithes_second_level)
            _params["hamming:switches_first_level"] = num_switches_first_level
            swap_keys = [("hamming:shape","shape"),("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos"),("hamming:link_width","link_width"),("hamming:switches_first_level","switches_first_level")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", self.global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            self.global_router_id = self.global_router_id + 1
            name_rtr = "rowx{}:{}x{}".format(row,0,router_level_1)
            list_routers_first_level[name_rtr] = rtr

            while ((col_idx != self.global_shape[1] * self.dims[1]) and current_down_port != 0):
                #print("While, {} {}".format(col_idx, current_down_port))
                if (self.isFirstOrLast(col_idx, self.dims[1])):
                    isFirst = self.isFirst(col_idx, self.dims[1])
                    # Connect from Fat Tree router to board router
                    unique_pos = self.global_to_local[self.GlobalToString([row, col_idx])]
                    partner_str = self.getRouterNameString((unique_pos))
                    #print("Fat Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                    rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%port, _params["link_lat"])
                    #rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%port, "1ns")
                    # Connect from board router to fat tree router
                    if (isFirst):
                        my_port = 3
                    else:
                        my_port = 1
                    #print("Fat Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                    other_rtr = self.list_routers[partner_str]
                    other_rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%my_port, _params["link_lat"])
                    #other_rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%my_port, "1ns")
                    port = port + 1
                    current_down_port = current_down_port - 1

                col_idx = col_idx + 1

        # Iterate routers second level and link them to first level switches
        col_idx = 0
        for router_level_2 in range(int(num_swithes_second_level)):
            # Create router
            current_down_port = num_ports_per_switch
            port = 0
            rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = num_ports_per_switch
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], [0,row], [1, router_level_2])
            _params["hamming:link_width"] = int(down_ports / num_swithes_second_level)
            _params["hamming:switches_first_level"] = num_switches_first_level
            swap_keys = [("hamming:shape","shape"),("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos"),("hamming:link_width","link_width"),("hamming:switches_first_level","switches_first_level")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", self.global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            self.global_router_id = self.global_router_id + 1
            name_rtr = "rowx{}:{}x{}".format(row,1,router_level_2)
            list_routers_second_level[name_rtr] = rtr
            
        starting_port_2_down = 0

        starting_port_down = [None] * num_swithes_second_level
        links_num = int(down_ports / num_swithes_second_level)

        for idx_2 in range(0, num_swithes_second_level):
            starting_port_down[idx_2] = list()
            for idx in range(0, num_switches_first_level):
                starting_port_down[idx_2].append(idx*links_num)

        starting_port_1_up = int(down_ports)

        #print("Links num {} - Starting Port Down {}".format(links_num, starting_port_down))

        for link_num in range(links_num):
            #starting_port_1_up = int(down_ports)
            for router_level_2 in range(num_swithes_second_level):
                for router_level_1 in range(num_switches_first_level):

                    # Connect from Fat Tree router to board router
                    name_rtr_1 = "rowx{}:{}x{}".format(row,0,router_level_1)
                    rtr_1 = list_routers_first_level[name_rtr_1]

                    name_rtr_2 = "rowx{}:{}x{}".format(row,1,router_level_2)
                    rtr_2 = list_routers_second_level[name_rtr_2]

                    #print("Fat2 Connecting1 {} to {} with port {}".format(name_rtr_1, name_rtr_2, starting_port_1_up))
                    rtr_1.addLink(getLink(name_rtr_1, name_rtr_2, link_num), "port%d"%starting_port_1_up, _params["link_lat"])
                    # Connect from board router to fat tree router
                    #print("Fat2 Connecting2 {} to {} with port {}".format(name_rtr_2, name_rtr_1, starting_port_down[router_level_2][router_level_1]))
                    rtr_2.addLink(getLink(name_rtr_1, name_rtr_2, link_num), "port%d"%starting_port_down[router_level_2][router_level_1], _params["link_lat"])

                    starting_port_down[router_level_2][router_level_1] = starting_port_down[router_level_2][router_level_1] + 1
                    #starting_port_2_down = starting_port_2_down + int(down_ports / num_swithes_second_level)
                    #print("starting_port_2_down {}".format(starting_port_2_down))

                starting_port_1_up = starting_port_1_up + 1   
                #print("starting_port_1_up {}".format(starting_port_1_up))

            starting_port_2_down = starting_port_2_down + 1
            #print("starting_port_2_down {}".format(starting_port_2_down))
        return 1

    def createColFatTree(self, nodes, total, row, col):

        # Helper function
        links = dict()
        def getLink(leftName, rightName, num):
            name = "link.%s:%s:%d"%(leftName, rightName, num)
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]

        # Determine if we are creating row or col wise fat Tree
        type_tree = ""
        if (row == -1):
            type_tree = "col"
        else:
            type_tree = "row"
        
        # Case 1:1, initialize overall values
        num_ports_per_switch = self.radix_fat_tree_switches
        num_switches_first_level =  int(math.ceil(nodes / (num_ports_per_switch / 2)))
        tot_ports_up = num_switches_first_level * (num_ports_per_switch / 2)
        num_swithes_second_level = int(math.ceil(tot_ports_up / num_ports_per_switch))
        down_ports = (num_ports_per_switch / 2)
        list_routers_first_level = {}
        list_routers_second_level = {}
        #print("Switches Level 1 {}, Up ports {} - Switches Level 2 {}".format(num_switches_first_level, tot_ports_up, num_swithes_second_level))
        
        # Iterate routers first level and link them to board switches
        row_idx = 0
        for router_level_1 in range(int(num_switches_first_level)):
            # Create router
            current_down_port = down_ports
            port = 0
            rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = num_ports_per_switch
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], [1,col], [0, router_level_1])
            _params["hamming:link_width"] = int(down_ports / num_swithes_second_level)
            _params["hamming:switches_first_level"] = num_switches_first_level
            swap_keys = [("hamming:shape","shape"),("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos"),("hamming:link_width","link_width"),("hamming:switches_first_level","switches_first_level")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", self.global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            self.global_router_id = self.global_router_id + 1
            name_rtr = "colx{}:{}x{}".format(col,0,router_level_1)
            list_routers_first_level[name_rtr] = rtr

            while ((row_idx != self.global_shape[0] * self.dims[0]) and current_down_port != 0):
                if (self.isFirstOrLast(row_idx, self.dims[0])):
                    isFirst = self.isFirst(row_idx, self.dims[0])
                    # Connect from Fat Tree router to board router
                    unique_pos = self.global_to_local[self.GlobalToString([row_idx, col])]
                    partner_str = self.getRouterNameString((unique_pos))
                    #print("Fat Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                    rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%port, _params["link_lat"])
                    # Connect from board router to fat tree router
                    if (isFirst):
                        my_port = 0
                    else:
                        my_port = 2
                    #print("Fat Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                    other_rtr = self.list_routers[partner_str]
                    other_rtr.addLink(getLink(name_rtr, partner_str, 0), "port%d"%my_port, _params["link_lat"])
                    port = port + 1
                    current_down_port = current_down_port - 1

                row_idx = row_idx + 1

        # Iterate routers second level and link them to first level switches
        row_idx = 0
        for router_level_2 in range(int(num_swithes_second_level)):
            # Create router
            current_down_port = num_ports_per_switch
            port = 0
            rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = num_ports_per_switch
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], [1,col], [1, router_level_2])
            _params["hamming:link_width"] = int(down_ports / num_swithes_second_level)
            _params["hamming:switches_first_level"] = num_switches_first_level
            swap_keys = [("hamming:shape","shape"),("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos"),("hamming:link_width","link_width"),("hamming:switches_first_level","switches_first_level")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", self.global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            self.global_router_id = self.global_router_id + 1
            name_rtr = "colx{}:{}x{}".format(col,1,router_level_2)
            list_routers_second_level[name_rtr] = rtr
            
        starting_port_2_down = 0

        starting_port_down = [None] * num_swithes_second_level
        links_num = int(down_ports / num_swithes_second_level)

        for idx_2 in range(0, num_swithes_second_level):
            starting_port_down[idx_2] = list()
            for idx in range(0, num_switches_first_level):
                starting_port_down[idx_2].append(idx*links_num)

        starting_port_1_up = int(down_ports)

        for link_num in range(links_num):
            #starting_port_1_up = int(down_ports)
            for router_level_2 in range(num_swithes_second_level):
                for router_level_1 in range(num_switches_first_level):

                    # Connect from Fat Tree router to board router
                    name_rtr_1 = "colx{}:{}x{}".format(col,0,router_level_1)
                    rtr_1 = list_routers_first_level[name_rtr_1]

                    name_rtr_2 = "colx{}:{}x{}".format(col,1,router_level_2)
                    rtr_2 = list_routers_second_level[name_rtr_2]

                    #print("Fat2 Connecting1 {} to {} with port {}".format(name_rtr_1, name_rtr_2, starting_port_1_up))
                    rtr_1.addLink(getLink(name_rtr_1, name_rtr_2, link_num), "port%d"%starting_port_1_up, _params["link_lat"])
                    # Connect from board router to fat tree router
                    #print("Fat2 Connecting2 {} to {} with port {}".format(name_rtr_2, name_rtr_1, starting_port_down[router_level_2][router_level_1]))
                    rtr_2.addLink(getLink(name_rtr_1, name_rtr_2, link_num), "port%d"%starting_port_down[router_level_2][router_level_1], _params["link_lat"])

                    starting_port_down[router_level_2][router_level_1] = starting_port_down[router_level_2][router_level_1] + 1

                starting_port_1_up = starting_port_1_up + 1   
                #print("starting_port_1_up {}".format(starting_port_1_up))

            starting_port_2_down = starting_port_2_down + 1
            #print("starting_port_2_down {}".format(starting_port_2_down))
        return 1


    def build(self):
        # Temp
        links = dict()
        def getLink(leftName, rightName):
            name = "link.%s:%s"%(leftName, rightName)
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]

        # Initialize variables
        local_router_id = 0
        glob_col_offest = 0
        glob_row_offest = 0
        tmp_boards_per_row = self.global_shape[1]

        # Start building each individual mesh, assign ids and connect links (leaving empty links for fat tree)
        for board_id in range(0, self.num_boards):
            for router_in_board in range(0, self.switch_per_board):
                mydims = self._idToLoc(local_router_id)
                my_loc_id = self.getLocalDims(local_router_id)
                my_glob_id = list(my_loc_id)
                #my_glob_id = my_loc_id.copy()
                my_glob_id[0] = my_loc_id[0] + glob_row_offest
                my_glob_id[1] = my_loc_id[1] + glob_col_offest
                unique_pos = self.getUniquePos(my_loc_id, board_id)
                #print("I am router {} (global) {} (local) - loc_pos {} - glob_pos {} - Unique pos {}".format(self.global_router_id, local_router_id, my_loc_id, my_glob_id, unique_pos))
                self.global_to_local[self.GlobalToString(my_glob_id)] = unique_pos

                # Create Router instance
                rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
                my_str = self.getRouterNameString(self.getUniquePos(my_loc_id, board_id))
                self.list_routers[my_str] = rtr

                # Add parameters to topology object
                self.setParameters(_params, True, self.global_router_id, local_router_id, my_glob_id, my_loc_id, unique_pos, -1, [-1, -1])
                swap_keys = [("hamming:shape","shape"), ("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:board_shape","board_shape"), ("hamming:width","width"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos"), ("hamming:algorithm","algorithm")]
                _topo_params = _params.subsetWithRename(swap_keys)
                rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
                rtr.addParam("id", self.global_router_id)
                topology = rtr.setSubComponent("topology","merlin.hamming")
                topology.addParams(_topo_params)

                # Start iterating in each direction and create links when appropriate 
                port = 0
                for direction in range(0, 4):
                    # Find Partner
                    offset = self.getOffsetPerDirection(direction)
                    #partner_pos = my_loc_id.copy()
                    partner_pos = list(my_loc_id)
                    partner_pos[0] = partner_pos[0] + offset[0]
                    partner_pos[1] = partner_pos[1] + offset[1]
                    partner_str = self.getRouterNameString(self.getUniquePos(partner_pos, board_id))
                    
                    # Create Links
                    if (self.isInsideBoard(partner_pos)):
                        if (direction <= 1):
                            #print("Yes Linking - I am {} - Partner is {} - Port {} - Link {}".format(my_str, partner_str, port, getLink(my_str, partner_str)))
                            #print("Link Latency to add is {}".format(_params["link_lat"]))
                            rtr.addLink(getLink(my_str, partner_str), "port%d"%port, _params["link_lat"])
                            #rtr.addLink(getLink(my_str, partner_str), "port%d"%port, "1ns")
                        else:
                            #print("Yes Linking - I am {} - Partner is {} - Port {} - Link {}".format(partner_str, my_str, port, getLink(partner_str, my_str)))
                            rtr.addLink(getLink(partner_str, my_str), "port%d"%port, _params["link_lat"])
                            #rtr.addLink(getLink(partner_str, my_str), "port%d"%port, "1ns")
                    #else:
                        #print("Not linking - I am {} - Partner is {}".format(my_str, partner_str))
                    port = port + 1
                
                # Create NIC connection
                for n in range(_params["hamming:local_ports"]):
                    nodeID = int(_params["hamming:local_ports"]) * self.global_router_id + n
                    #print("Building NIC {} in router {} using port {}".format(nodeID, self.global_router_id, port))
                    ep = self._getEndPoint(nodeID).build(nodeID, {})
                    if ep:
                        nicLink = sst.Link("nic.%d:%d"%(self.global_router_id, n))
                        if self.bundleEndpoints:
                            nicLink.setNoCut()
                        #print("Nic Link Latency to add is {}".format(_params["link_lat"]))
                        nicLink.connect(ep, (rtr, "port%d"%port, _params["link_lat"]))
                        #nicLink.connect(ep, (rtr, "port%d"%port, "1ns"))
                    port = port+1
                
                # Update IDs
                local_router_id = local_router_id + 1
                self.global_router_id = self.global_router_id + 1

            # Decrease how many boards we have left per this row or start a new row
            tmp_boards_per_row = tmp_boards_per_row - 1
            if (tmp_boards_per_row > 0):
                glob_col_offest = glob_col_offest +  self.dims[1]
            elif (tmp_boards_per_row == 0):
                glob_col_offest = 0
                tmp_boards_per_row = self.global_shape[1]
                glob_row_offest = glob_row_offest +  self.dims[0]
            # New Board, reset local router id 
            local_router_id = 0

        total_rows = self.global_shape[0] * self.dims[0]
        total_cols = self.global_shape[1] * self.dims[1]
        # Create fat tree,  first row wise
        for row in range(total_rows):
            # Iterate all routers of this row and connect them to the router
            if (self.global_shape[1] * 2 > self.radix_fat_tree_switches):
                self.createRowFatTree(self.global_shape[1] * 2, total_cols, row, -1)
            else:
                # Here, for now, we just create a single router to connect everything
                # Create single Router instance
                port = 0
                rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
                _params["num_ports"] = _params["router_radix"] = self.global_shape[1] * 2
                _params["hamming:local_ports"] = 0
                self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1],  [[-1, -1], [-1, -1]], [0,row], [0, 0])
                _params["hamming:single_switch_fat_tree"] = True
                swap_keys = [("hamming:algorithm","algorithm"), ("hamming:single_switch_fat_tree","single_switch_fat_tree"),("hamming:shape","shape"),("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos")]
                _topo_params = _params.subsetWithRename(swap_keys)
                rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
                rtr.addParam("id", self.global_router_id)
                topology = rtr.setSubComponent("topology","merlin.hamming")
                topology.addParams(_topo_params)
                self.global_router_id = self.global_router_id + 1
                name_rtr = "rowx{}".format(row)

                # Iterate all routers of this row and connect them to the router
                for col in range(total_cols):
                    if (self.isFirstOrLast(col, self.dims[1])):
                        # Get if we are connecting to the first or last router
                        isFirst = self.isFirst(col, self.dims[1])
                        # Connect from Fat Tree router to board router
                        unique_pos = self.global_to_local[self.GlobalToString([row, col])]
                        partner_str = self.getRouterNameString((unique_pos))

                        #print("Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                        rtr.addLink(getLink(name_rtr, partner_str), "port%d"%port, _params["link_lat"])

                        # Connect from board router to fat tree router
                        if (isFirst):
                            my_port = 3
                        else:
                            my_port = 1

                        #print("Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                        other_rtr = self.list_routers[partner_str]
                        other_rtr.addLink(getLink(name_rtr, partner_str), "port%d"%my_port, _params["link_lat"])

                        port = port + 1
                    

        # Fat Tree Col wise
        for col in range(total_cols):
            # Here, for now, we just create a single router to connect everything
            # Create single Router instance
            if (self.global_shape[0] * 2 > self.radix_fat_tree_switches):
                self.createColFatTree(self.global_shape[0] * 2, total_rows, -1, col)
            else:
                # Here, for now, we just create a single router to connect everything
                # Create single Router instance
                port = 0
                rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
                _params["num_ports"] = _params["router_radix"] = self.global_shape[0] * 2
                _params["hamming:local_ports"] = 0
                self.setParameters(_params, False, self.global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], [1,col], [0, 0])
                _params["hamming:single_switch_fat_tree"] = True
                swap_keys = [("hamming:algorithm","algorithm"), ("hamming:single_switch_fat_tree","single_switch_fat_tree"),("hamming:shape","shape"),("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos")]
                _topo_params = _params.subsetWithRename(swap_keys)
                rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
                rtr.addParam("id", self.global_router_id)
                topology = rtr.setSubComponent("topology","merlin.hamming")
                topology.addParams(_topo_params)
                self.global_router_id = self.global_router_id + 1
                name_rtr = "colx{}".format(col)

                # Iterate all routers of this row and connect them to the router
                for row in range(total_rows):
                    if (self.isFirstOrLast(row, self.dims[0])):
                        # Get if we are connecting to the first or last router
                        isFirst = self.isFirst(row, self.dims[0])
                        # Connect from Fat Tree router to board router
                        unique_pos = self.global_to_local[self.GlobalToString([row, col])]
                        partner_str = self.getRouterNameString((unique_pos))

                        #print("Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                        rtr.addLink(getLink(name_rtr, partner_str), "port%d"%port, _params["link_lat"])

                        # Connect from board router to fat tree router
                        if (isFirst):
                            my_port = 0
                        else:
                            my_port = 2

                        #print("Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                        other_rtr = self.list_routers[partner_str]
                        other_rtr.addLink(getLink(name_rtr, partner_str), "port%d"%my_port, _params["link_lat"])

                        port = port + 1



        '''
        total_rows = self.global_shape[0] * self.dims[0]
        total_cols = self.global_shape[1] * self.dims[1]
        # Create fat tree,  first row wise
        for row in range(total_rows):
            # Here, for now, we just create a single router to connect everything
            # Create single Router instance
            port = 0
            rtr = self._instanceRouter(self.global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = self.global_shape[1] * 2
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, global_router_id, -1, [-1,-1], [-1,-1],  [[-1, -1], [-1, -1]], -1, [0, row])
            swap_keys = [("hamming:shape","shape"),("hamming:fat_tree_shape","fat_tree_shape"), ("hamming:board_shape","board_shape"),("hamming:width","width"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            global_router_id = global_router_id + 1
            name_rtr = "rowx{}".format(row)

            # Iterate all routers of this row and connect them to the router
            for col in range(total_cols):
                if (self.isFirstOrLast(col, self.dims[1])):
                    # Get if we are connecting to the first or last router
                    isFirst = self.isFirst(col, self.dims[1])
                    # Connect from Fat Tree router to board router
                    unique_pos = self.global_to_local[self.GlobalToString([row, col])]
                    partner_str = self.getRouterNameString((unique_pos))

                    #print("Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                    rtr.addLink(getLink(name_rtr, partner_str), "port%d"%port, _params["link_lat"])

                    # Connect from board router to fat tree router
                    if (isFirst):
                        my_port = 3
                    else:
                        my_port = 1

                    print("Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                    other_rtr = self.list_routers[partner_str]
                    other_rtr.addLink(getLink(name_rtr, partner_str), "port%d"%my_port, _params["link_lat"])

                    port = port + 1

        
        # Fat Tree Col wise
        for col in range(total_cols):
            # Here, for now, we just create a single router to connect everything
            # Create single Router instance
            port = 0
            rtr = self._instanceRouter(global_router_id,"merlin.hr_router")
            _params["num_ports"] = _params["router_radix"] = self.global_shape[0] * 2
            _params["hamming:local_ports"] = 0
            self.setParameters(_params, False, global_router_id, -1, [-1,-1], [-1,-1], [[-1, -1], [-1, -1]], -1, [1, col])
            swap_keys = [("hamming:shape","shape"),("hamming:width","width"), ("hamming:board_shape","board_shape"),("hamming:local_ports","local_ports"),("hamming:global_shape","global_shape"),("hamming:is_board_switch","is_board_switch"),("hamming:global_switch_id","global_switch_id"),("hamming:local_switch_id","local_switch_id"),("hamming:global_pos","global_pos"),("hamming:local_pos","local_pos"),("hamming:unique_pos","unique_pos"),("hamming:fat_tree_id","fat_tree_id"),("hamming:fat_tree_pos","fat_tree_pos")]
            _topo_params = _params.subsetWithRename(swap_keys)
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", global_router_id)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)
            global_router_id = global_router_id + 1
            name_rtr = "colx{}".format(col)

            # Iterate all routers of this row and connect them to the router
            for row in range(total_rows):
                if (self.isFirstOrLast(row, self.dims[0])):
                    # Get if we are connecting to the first or last router
                    isFirst = self.isFirst(row, self.dims[0])
                    # Connect from Fat Tree router to board router
                    unique_pos = self.global_to_local[self.GlobalToString([row, col])]
                    partner_str = self.getRouterNameString((unique_pos))

                    print("Connecting {} to {} with port {}".format(name_rtr, partner_str, port))
                    rtr.addLink(getLink(name_rtr, partner_str), "port%d"%port, _params["link_lat"])

                    # Connect from board router to fat tree router
                    if (isFirst):
                        my_port = 0
                    else:
                        my_port = 2

                    print("Connecting {} to {} with port {}".format(partner_str, name_rtr, my_port))
                    other_rtr = self.list_routers[partner_str]
                    other_rtr.addLink(getLink(name_rtr, partner_str), "port%d"%my_port, _params["link_lat"])

                    port = port + 1
        
        num_routers = self.getTotRoutersMeshes()
        for i in range(num_routers):
            # set up 'mydims'
            mydims = self._idToLoc(i)
            mylocstr = self._formatShape(mydims)
            print(mylocstr)

            rtr = self._instanceRouter(i,"merlin.hr_router")
            #rtr = sst.Component("rtr.%s"%mylocstr, "merlin.hr_router")
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", i)
            topology = rtr.setSubComponent("topology","merlin.hamming")
            topology.addParams(_topo_params)

            port = 0
            if (i < self.switch_per_board):
                for dim in range(self.nd):
                    theirdims = mydims[:]

                    # Positive direction
                    theirdims[dim] = (mydims[dim] +1 ) % self.dims[dim]
                    #print("Positive - I {}  Port {} mydims {} dims {} mylocstr {} theirdim {} dimwidths[dim] {}".format(str(i), port, 
                    #mydims,self.dims, mylocstr, theirdims, self.dimwidths[dim]))
                    if (mydims[dim] + 1 <= self.dims[dim] - 1):
                        theirlocstr = self._formatShape(theirdims)
                        print("theirlocstr {} - getLink {}".format(theirlocstr, getLink(mylocstr, theirlocstr, 0)))
                        for num in range(self.dimwidths[dim]):
                            print("Yes Linking {} - I am {} - Partner is {} - Port {}".format(i, mylocstr, theirlocstr, port))
                            rtr.addLink(getLink(mylocstr, theirlocstr, num), "port%d"%port, _params["link_lat"])
                            port = port+1
                    else:
                        print("not linking")
                        port = port+1

                    # Negative direction
                    theirdims[dim] = ((mydims[dim] -1) +self.dims[dim]) % self.dims[dim]
                    #print("Negative - Router {} Port {} mydims {} dims {} mylocstr {} theirdim {}".format(str(i), port, mydims,self.dims, mylocstr, theirdims))
                    if ((mydims[dim] - 1) >= 0):
                        theirlocstr = self._formatShape(theirdims)
                        for num in range(self.dimwidths[dim]):
                            print("Yes Linking {} - I am {} - Partner is {} - Port {}".format(i, theirlocstr, mylocstr, port))
                            rtr.addLink(getLink(theirlocstr, mylocstr, num), "port%d"%port, _params["link_lat"])
                            port = port+1
                    else:
                        print("not linking2")
                        port = port+1

            for n in range(_params["hamming:local_ports"]):
                nodeID = int(_params["hamming:local_ports"]) * i + n
                print("Building NIC {} in router {} using port {}".format(nodeID, i, port))

                ep = self._getEndPoint(nodeID).build(nodeID, {})
                if ep:
                    nicLink = sst.Link("nic.%d:%d"%(i, n))
                    if self.bundleEndpoints:
                        nicLink.setNoCut()
                    nicLink.connect(ep, (rtr, "port%d"%port, _params["link_lat"]))
                port = port+1'''


class topoHyperX(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys = ["topology", "debug", "num_ports", "flit_size", "nic_link_bw", "link_bw", "xbar_bw", "hyperx:shape", "hyperx:width", "hyperx:local_ports","input_latency","output_latency","input_buf_size","output_buf_size"]
        self.topoOptKeys = ["xbar_arb","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"]
    def getName(self):
        return "HyperX"
    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        peers = 1
        radix = 0
        self.dims = []
        self.dimwidths = []
        if not "hyperx:shape" in _params:
            self.nd = int(_params["num_dims"])
            for x in range(self.nd):
                print("Dim %d size:"%x)
                ds = int(input())
                self.dims.append(ds);
            _params["hyperx:shape"] = self._formatShape(self.dims)
        else:
            self.dims = [int(x) for x in _params["hyperx:shape"].split('x')]
            self.nd = len(self.dims)
        if not "hyperx:width" in _params:
            for x in range(self.nd):
                print("Dim %d width (# of links in this dimension):" % x)
                dw = int(input())
                self.dimwidths.append(dw)
            _params["hyperx:width"] = self._formatShape(self.dimwidths)
        else:
            self.dimwidths = [int(x) for x in _params["hyperx:width"].split('x')]

        local_ports = int(_params["hyperx:local_ports"])
        radix = local_ports
        for x in range(self.nd):
            radix += (self.dimwidths[x] * (self.dims[x]-1))

        for x in self.dims:
            peers = peers * x
        peers = peers * local_ports

        _params["num_peers"] = peers
        _params["num_dims"] = self.nd
        _params["topology"] = _params["topology"] = "merlin.hyperx"
        _params["debug"] = debug
        _params["num_ports"] = _params["router_radix"] = radix
        _params["hyperx:local_ports"] = local_ports

    def _formatShape(self, arr):
        return 'x'.join([str(x) for x in arr])

    def _idToLoc(self,rtr_id):
        foo = list()
        for i in range(self.nd-1, 0, -1):
            div = 1
            for j in range(0, i):
                div = div * self.dims[j]
            value = (rtr_id // div)
            foo.append(value)
            rtr_id = rtr_id - (value * div)
        foo.append(rtr_id)
        foo.reverse()
        return foo

    def getRouterNameForId(self,rtr_id):
        return self.getRouterNameForLocation(self._idToLoc(rtr_id))

    def getRouterNameForLocation(self,location):
        return "rtr.%s"%(self._formatShape(location))
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location));
    
    
    def build(self):
        num_routers = _params["num_peers"] // _params["hyperx:local_ports"]
        links = dict()
        def getLink(name1, name2, num):
            # Sort name1 and name2 so order doesn't matter
            if str(name1) < str(name2):
                name = "link.%s:%s:%d"%(name1, name2, num)
            else:
                name = "link.%s:%s:%d"%(name2, name1, num)
            if name not in links:
                links[name] = sst.Link(name)
            #print("Getting link with name: %s"%name)
            return links[name]

        swap_keys = [("hyperx:shape","shape"),("hyperx:width","width"),("hyperx:local_ports","local_ports"),("hyperx:algorithm","algorithm")]

        _topo_params = _params.subsetWithRename(swap_keys);

        # loop through the routers to hook up links
        for i in range(num_routers):
            # set up 'mydims'
            mydims = self._idToLoc(i)
            mylocstr = self._formatShape(mydims)

            #print("Creating router %s (%d)"%(mylocstr,i))

            rtr = self._instanceRouter(i,"merlin.hr_router")
            #rtr = sst.Component("rtr.%s"%mylocstr, "merlin.hr_router")
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id", i)
            topology = rtr.setSubComponent("topology","merlin.hyperx")
            topology.addParams(_topo_params)

            port = 0
            # Connect to all routers that only differ in one location index
            for dim in range(self.nd):
                theirdims = mydims[:]

                # We have links to every other router in each dimension
                for router in range(self.dims[dim]):
                    if router != mydims[dim]: # no link to ourselves
                        theirdims[dim] = router
                        theirlocstr = self._formatShape(theirdims)
                        # Hook up "width" number of links for this dimension
                        for num in range(self.dimwidths[dim]):
                            rtr.addLink(getLink(mylocstr, theirlocstr, num), "port%d"%port, _params["link_lat"])
                            #print("Wired up port %d"%port)
                            port = port + 1


            for n in range(_params["hyperx:local_ports"]):
                nodeID = int(_params["hyperx:local_ports"]) * i + n
                ep = self._getEndPoint(nodeID).build(nodeID, {})
                if ep:
                    nicLink = sst.Link("nic.%d:%d"%(i, n))
                    if self.bundleEndpoints:
                       nicLink.setNoCut()
                    nicLink.connect(ep, (rtr, "port%d"%port, _params["link_lat"]))
                port = port+1





class topoFatTree(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys = ["topology", "debug", "flit_size", "link_bw", "xbar_bw","nic_link_bw", "input_latency","output_latency","input_buf_size","output_buf_size", "fattree:shape"]
        self.topoOptKeys = ["xbar_arb", "fattree:routing_alg", "fattree:adaptive_threshold","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"]
        self.nicKeys = ["link_bw"]
        self.ups = []
        self.downs = []
        self.routers_per_level = []
        self.groups_per_level = []
        self.start_ids = []


    def getName(self):
        return "Fat Tree"

    def prepParams(self):
#        if "xbar_arb" in _params:
#            self.rtrKeys.append("xbar_arb")
            #            _params["xbar_arb"] = "merlin.xbar_arb_lru"
#        if "fattree:routing_alg" in _params:
#            self.rtrKeys.append("fattree:routing_alg")
#        if "fattree:adaptive_threshold" in _params:
#            self.rtrKeys.append("fattree:adaptive_threshold")

        self.shape = _params["fattree:shape"]

        levels = self.shape.split(":")

        #print(levels)

        for l in levels:
            links = l.split(",")

            #print(links)

            self.downs.append(int(links[0]))
            if len(links) > 1:
                self.ups.append(int(links[1]))

        #print(self.downs)
        #print(self.ups)

        self.total_hosts = 1
        for i in self.downs:
            self.total_hosts *= i

        #print("Total hosts: " + str(self.total_hosts))

        self.routers_per_level = [0] * len(self.downs)
        self.routers_per_level[0] = self.total_hosts // self.downs[0]
        for i in range(1,len(self.downs)):
            self.routers_per_level[i] = self.routers_per_level[i-1] * self.ups[i-1] // self.downs[i]

        self.start_ids = [0] * len(self.downs)
        for i in range(1,len(self.downs)):
            self.start_ids[i] = self.start_ids[i-1] + self.routers_per_level[i-1]

        self.groups_per_level = [1] * len(self.downs);
        if self.ups: # if ups is empty, then this is a single level and the following line will fail
            self.groups_per_level[0] = self.total_hosts // self.downs[0]

        for i in range(1,len(self.downs)-1):
            self.groups_per_level[i] = self.groups_per_level[i-1] // self.downs[i]

        #print(self.groups_per_level)
        _params["debug"] = debug
        _params["topology"] = "merlin.fattree"
        _params["num_peers"] = self.total_hosts


    def getRouterNameForId(self,rtr_id):
        num_levels = len(self.start_ids)

        # Check to make sure the index is in range
        level = num_levels - 1
        if rtr_id >= (self.start_ids[level] + self.routers_per_level[level]) or rtr_id < 0:
            print("ERROR: topoFattree.getRouterNameForId: rtr_id not found: %d"%rtr_id)
            sst.exit()

        # Find the level
        for x in range(num_levels-1,0,-1):
            if rtr_id >= self.start_ids[x]:
                break
            level = level - 1

        # Find the group
        remainder = rtr_id - self.start_ids[level]
        routers_per_group = self.routers_per_level[level] // self.groups_per_level[level]
        group = remainder // routers_per_group
        router = remainder % routers_per_group
        return self.getRouterNameForLocation((level,group,router))
            
    
    def getRouterNameForLocation(self,location):
        return "rtr_l%s_g%d_r%d"%(location[0],location[1],location[2])
    
    def findRouterByLocation(self,location):
        return sst.findComponentByName(self.getRouterNameForLocation(location));
    
        
    def fattree_rb(self, level, group, links):
#        print("routers_per_level: %d, groups_per_level: %d, start_ids: %d"%(self.routers_per_level[level],self.groups_per_level[level],self.start_ids[level]))
        id = self.start_ids[level] + group * (self.routers_per_level[level]//self.groups_per_level[level])
        #print("start id: " + str(id))


        host_links = []
        if level == 0:
            # create all the nodes
            for i in range(self.downs[0]):
                node_id = id * self.downs[0] + i
                #print("group: %d, id: %d, node_id: %d"%(group, id, node_id))
                ep = self._getEndPoint(node_id).build(node_id, {})
                if ep:
                    hlink = sst.Link("hostlink_%d"%node_id)
                    if self.bundleEndpoints:
                       hlink.setNoCut()
                    ep[0].addLink(hlink, ep[1], ep[2])
                    host_links.append(hlink)

            # Create the edge router
            rtr_id = id

            rtr = self._instanceRouter(rtr_id,"merlin.hr_router")
            #rtr = sst.Component("rtr_l0_g%d_r0"%(group), "merlin.hr_router")

            # Add parameters
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id",rtr_id)
            rtr.addParam("num_ports",self.ups[0] + self.downs[0])
            topology = rtr.setSubComponent("topology","merlin.fattree")
            topology.addParams(self._topo_params)
            # Add links
            for l in range(len(host_links)):
                rtr.addLink(host_links[l],"port%d"%l, _params["link_lat"])
            for l in range(len(links)):
                rtr.addLink(links[l],"port%d"%(l+self.downs[0]), _params["link_lat"])
            return

        rtrs_in_group = self.routers_per_level[level] // self.groups_per_level[level]
        # Create the down links for the routers
        rtr_links = [ [] for index in range(rtrs_in_group) ]
        for i in range(rtrs_in_group):
            for j in range(self.downs[level]):
#                print("Creating link: link_l%d_g%d_r%d_p%d"%(level,group,i,j);)
                rtr_links[i].append(sst.Link("link_l%d_g%d_r%d_p%d"%(level,group,i,j)));

        # Now create group links to pass to lower level groups from router down links
        group_links = [ [] for index in range(self.downs[level]) ]
        for i in range(self.downs[level]):
            for j in range(rtrs_in_group):
                group_links[i].append(rtr_links[j][i])

        for i in range(self.downs[level]):
            self.fattree_rb(level-1,group*self.downs[level]+i,group_links[i])

        # Create the routers in this level.
        # Start by adding up links to rtr_links
        for i in range(len(links)):
            rtr_links[i % rtrs_in_group].append(links[i])

        for i in range(rtrs_in_group):
            rtr_id = id + i

            rtr = self._instanceRouter(rtr_id,"merlin.hr_router")
            #rtr = sst.Component("rtr_l%d_g%d_r%d"%(level,group,i), "merlin.hr_router")

            # Add parameters
            rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
            rtr.addParam("id",rtr_id)
            rtr.addParam("num_ports",self.ups[level] + self.downs[level])
            topology = rtr.setSubComponent("topology","merlin.fattree")
            topology.addParams(self._topo_params)
            # Add links
            for l in range(len(rtr_links[i])):
                rtr.addLink(rtr_links[i][l],"port%d"%l, _params["link_lat"])

    def build(self):
#        print("build()")

        swap_keys = [("fattree:shape","shape"),("fattree:algorithm","algorithm"),("fattree:adaptive_threshold","adaptive_threshold")]

        self._topo_params = _params.subsetWithRename(swap_keys);


        level = len(self.ups)
        if self.ups: # True for all cases except for single level
            #  Create the router links
            rtrs_in_group = self.routers_per_level[level] // self.groups_per_level[level]
            #print(rtrs_in_group)
            # Create the down links for the routers
            rtr_links = [ [] for index in range(rtrs_in_group) ]
            for i in range(rtrs_in_group):
                for j in range(self.downs[level]):
#                    print("Creating link: link_l%d_g0_r%d_p%d"%(level,i,j))
                    rtr_links[i].append(sst.Link("link_l%d_g0_r%d_p%d"%(level,i,j)));

            # Now create group links to pass to lower level groups from router down links
            group_links = [ [] for index in range(self.downs[level]) ]
            for i in range(self.downs[level]):
                for j in range(rtrs_in_group):
                    group_links[i].append(rtr_links[j][i])


            for i in range(self.downs[len(self.ups)]):
                self.fattree_rb(level-1,i,group_links[i])

            # Create the routers in this level
            radix = self.downs[level]
            for i in range(self.routers_per_level[level]):

                rtr_id = self.start_ids[len(self.ups)] + i

                rtr = self._instanceRouter(rtr_id,"merlin.hr_router")
                #rtr = sst.Component("rtr_l%d_g0_r%d"%(len(self.ups),i),"merlin.hr_router")

                rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
                rtr.addParam("id", rtr_id)
                rtr.addParam("num_ports",radix)
                topology = rtr.setSubComponent("topology","merlin.fattree")
                topology.addParams(self._topo_params)

                for l in range(len(rtr_links[i])):
                    rtr.addLink(rtr_links[i][l], "port%d"%l, _params["link_lat"])

        else: # Single level case
            # create all the nodes
            for i in range(self.downs[0]):
                node_id = i
#                print("Instancing node " + str(node_id))
        rtr_id = 0
#        print("Instancing router " + str(rtr_id))





class topoDragonFly(Topo):
    def __init__(self):
        Topo.__init__(self)
        self.topoKeys = ["topology", "debug", "num_ports", "flit_size", "link_bw","nic_link_bw","xbar_bw", "dragonfly:hosts_per_router", "dragonfly:routers_per_group", "dragonfly:intergroup_per_router", "dragonfly:num_groups","dragonfly:intergroup_links","input_latency","output_latency","input_buf_size","output_buf_size","dragonfly:global_route_mode"]
        self.topoOptKeys = ["xbar_arb","link_bw:host","link_bw:group","link_bw:global","input_latency:host","input_latency:group","input_latency:global","output_latency:host","output_latency:group","output_latency:global","input_buf_size:host","input_buf_size:group","input_buf_size:global","output_buf_size:host","output_buf_size:group","output_buf_size:global","num_vns","vn_remap","vn_remap_shm","portcontrol:output_arb","portcontrol:arbitration:qos_settings","portcontrol:arbitration:arb_vns","portcontrol:arbitration:arb_vcs"]
        self.global_link_map = None
        self.global_routes = "absolute"

    def getName(self):
        return "Dragonfly"

    def prepParams(self):
#        if "xbar_arb" not in _params:
#            _params["xbar_arb"] = "merlin.xbar_arb_lru"
        _params["topology"] = "merlin.dragonfly"
        _params["debug"] = debug
#        _params["router_radix"] = int(_params["router_radix"])
        _params["dragonfly:hosts_per_router"] = int(_params["dragonfly:hosts_per_router"])
        _params["dragonfly:routers_per_group"] = int(_params["dragonfly:routers_per_group"])
        _params["dragonfly:intergroup_links"] = int(_params["dragonfly:intergroup_links"])
        _params["dragonfly:num_groups"] = int(_params["dragonfly:num_groups"])
        _params["num_peers"] = _params["dragonfly:hosts_per_router"] * _params["dragonfly:routers_per_group"] * _params["dragonfly:num_groups"]
        _params["dragonfly:global_route_mode"] = self.global_routes


        self.total_intergroup_links = (_params["dragonfly:num_groups"] - 1) * _params["dragonfly:intergroup_links"]
        _params["dragonfly:intergroup_per_router"] = int((self.total_intergroup_links + _params["dragonfly:routers_per_group"] - 1 ) // _params["dragonfly:routers_per_group"])
        self.empty_ports = _params["dragonfly:intergroup_per_router"] * _params["dragonfly:routers_per_group"] - self.total_intergroup_links
        #print("Per router global -> " + str(_params["dragonfly:intergroup_per_router"]))

        _params["router_radix"] = _params["dragonfly:routers_per_group"]-1 + _params["dragonfly:hosts_per_router"] + _params["dragonfly:intergroup_per_router"]
        _params["num_ports"] = int(_params["router_radix"])

        if _params["dragonfly:num_groups"] > 2:
            foo = _params["dragonfly:algorithm"]
            self.topoKeys.append("dragonfly:algorithm")

    def setGlobalLinkMap(self, glm):
        self.global_link_map = glm

    def setRoutingModeAbsolute(self):
        self.global_routes = "absolute"

    def setRoutingModeRelative(self):
        self.global_routes = "relative"


    def getRouterNameForId(self,rtr_id):
        rpg = _params["dragonfly:routers_per_group"]
        ret = self.getRouterNameForLocation(rtr_id // rpg, rtr_id % rpg)
        return ret
        
    def getRouterNameForLocation(self,group,rtr):
        return "rtr:G%dR%d"%(group,rtr)
    
    def findRouterByLocation(self,group,rtr):
        return sst.findComponentByName(self.getRouterNameForLocation(group,rtr))
    
    def build(self):
        links = dict()

        #####################
        def getLink(name):
            if name not in links:
                links[name] = sst.Link(name)
            return links[name]
        #####################

        rpg = _params["dragonfly:routers_per_group"]
        ng = _params["dragonfly:num_groups"] - 1 # don't count my group
        igpr = _params["dragonfly:intergroup_per_router"]

        if self.global_link_map is None:
            # Need to define global link map

            self.global_link_map = [-1 for i in range(igpr * rpg)]

            # Links will be mapped in linear order, but we will
            # potentially skip one port per router, depending on the
            # parameters.  The variable self.empty_ports tells us how
            # many routers will have one global port empty.
            count = 0
            start_skip = rpg - self.empty_ports
            for i in range(0,rpg):
                # Determine if we skip last port for this router
                end = igpr
                if i >= start_skip:
                    end = end - 1
                for j in range(0,end):
                    self.global_link_map[i*igpr+j] = count;
                    count = count + 1

        #print("Global link map array")
        #print(self.global_link_map)

        # End set global link map with default


        # g is group number
        # r is router number with group
        # p is port number relative to start of global ports

        def getGlobalLink(g, r, p):
            # Look into global link map to get the dest group and link
            # number to that group
            raw_dest = self.global_link_map[r * igpr + p];
            if raw_dest == -1:
                return None

            # Turn raw_dest into dest_grp and link_num
            link_num = raw_dest // ng;
            dest_grp = raw_dest - link_num * ng

            if ( self.global_routes == "absolute" ):
                # Compute dest group ignoring my own group id, for a
                # dest_grp >= g, we need to add 1 to get the right group
                if dest_grp >= g:
                    dest_grp = dest_grp + 1
            elif ( self.global_routes == "relative"):
                # For relative, add current group to dest_grp + 1 and
                # do modulo of num_groups to get actual group
                dest_grp = (dest_grp + g + 1) % (ng+1)
            #else:
                # should never happen

            src = min(dest_grp, g)
            dest = max(dest_grp, g)

            #getLink("link:g%dg%dr%d"%(g, src, dst)), "port%d"%port, _params["link_lat"])
            return getLink("link:g%dg%dr%d"%(src,dest,link_num))

        #########################

        swap_keys = [("dragonfly:hosts_per_router","hosts_per_router"),("dragonfly:routers_per_group","routers_per_group"),("dragonfly:intergroup_links","intergroup_links"),("dragonfly:num_groups","num_groups"),("dragonfly:intergroup_per_router","intergroup_per_router"),("dragonfly:algorithm","algorithm"),("dragonfly:global_route_mode","global_route_mode"),("dragonfly:adaptive_threshold","adaptive_threshold")]

        _topo_params = _params.subsetWithRename(swap_keys);

        router_num = 0
        nic_num = 0
        # GROUPS
        for g in range(_params["dragonfly:num_groups"]):

            # GROUP ROUTERS
            for r in range(_params["dragonfly:routers_per_group"]):
                rtr = self._instanceRouter(router_num,"merlin.hr_router")
                #rtr = sst.Component("rtr:G%dR%d"%(g, r), "merlin.hr_router")
                rtr.addParams(_params.subset(self.topoKeys, self.topoOptKeys))
                rtr.addParam("id", router_num)
                topology = rtr.setSubComponent("topology","merlin.dragonfly")
                topology.addParams(_topo_params)
                if router_num == 0:
                    # Need to send in the global_port_map
                    #map_str = str(self.global_link_map).strip('[]')
                    #rtr.addParam("dragonfly:global_link_map",map_str)
                    rtr.addParam("dragonfly:global_link_map",self.global_link_map)
                    topology.addParam("global_link_map",self.global_link_map)

                port = 0
                for p in range(_params["dragonfly:hosts_per_router"]):
                    ep = self._getEndPoint(nic_num).build(nic_num, {})
                    if ep:
                        link = sst.Link("link:g%dr%dh%d"%(g, r, p))
                        if self.bundleEndpoints:
                            link.setNoCut()
                        link.connect(ep, (rtr, "port%d"%port, _params["link_lat"]) )
                    nic_num = nic_num + 1
                    port = port + 1

                for p in range(_params["dragonfly:routers_per_group"]):
                    if p != r:
                        src = min(p,r)
                        dst = max(p,r)
                        rtr.addLink(getLink("link:g%dr%dr%d"%(g, src, dst)), "port%d"%port, _params["link_lat"])
                        port = port + 1

                for p in range(_params["dragonfly:intergroup_per_router"]):
                    link = getGlobalLink(g,r,p)
                    if link is not None:
                        rtr.addLink(link,"port%d"%port, _params["link_lat"])
                    port = port +1

                router_num = router_num +1



############################################################################

class EndPoint(object):
    def __init__(self):
        self.epKeys = []
        self.epOptKeys = []
        self.enableAllStats = False
        self.statInterval = "0"
    def getName(self):
        print("Not implemented")
        sys.exit(1)
    def prepParams(self):
        pass
    def build(self, nID, extraKeys):
        return None

class TestEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        #self.nicKeys = ["topology", "num_peers", "num_messages", "link_bw", "checkerboard"]
        self.epKeys.extend(["num_peers", "link_bw"])
        self.epOptKeys.extend(["checkerboard", "num_messages"])
        self.split = 1
        self.group_array = None

    def divide(self,split):
        self.split = split

    def getName(self):
        return "Test End Point"

    def prepParams(self):
        #if "checkerboard" not in _params:
        #    _params["checkerboard"] = "1"
        #if "num_messages" not in _params:
        #    _params["num_messages"] = "10"
        pass

    def build(self, nID, extraKeys):
        # Copmute group size and offset
        if self.group_array is None:
            num_ep = sst.merlin._params["num_peers"]
            min_per_group = num_ep // self.split
            self.group_array = [min_per_group] * self.split
            num_ep = num_ep - ( min_per_group * self.split )
            for i in range(num_ep):
                self.group_array[i] = self.group_array[i] + 1


        nic = sst.Component("testNic.%d"%nID, "merlin.test_nic")
        linkif = nic.setSubComponent("networkIF","merlin.linkcontrol")
        if ( "link_bw" in _params):
            linkif.addParam("link_bw",_params["link_bw"])
        #if ( "input_buf_size" in _params):
        #    linkif.addParam("input_buf_size",_params["input_buf_size"])
        #if ( "output_buf_size" in _params):
        #    linkif.addParam("output_buf_size",_params["output_buf_size"])
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(extraKeys))
        nic.addParam("id", nID)
        if self.split != 1:
            # Need to figure out what group I'm in
            limit = 0
            offset = 0
            for i in range(self.split):
                limit = limit + self.group_array[i]
                if nID < limit:
                    nic.addParam("group_peers",self.group_array[i])
                    nic.addParam("group_offset",offset)
                    break
                offset = offset + self.group_array[i]
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic","rate":self.statInterval})
        return (linkif, "rtr_port", _params["link_lat"])
        #print("Created Endpoint with id: %d, and params: %s %s\n"%(nID, _params.subset(self.nicKeys), _params.subset(extraKeys)))
    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;

class BisectionEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        self.epKeys.extend(["num_peers", "link_bw", "packet_size", "packets_to_send", "buffer_size"])
        self.epOptKeys.extend(["checkerboard", "rlc:networkIF"])

    def getName(self):
        return "Bisection Test End Point"

    def prepParams(self):
        #if "checkerboard" not in _params:
        #    _params["checkerboard"] = "1"
        pass

    def build(self, nID, extraKeys):
        nic = sst.Component("bisectionNic.%d"%nID, "merlin.bisection_test")
        linkif = nic.setSubComponent("networkIF","merlin.linkcontrol")
        if ( "link_bw" in _params):
            linkif.addParam("link_bw",_params["link_bw"])
        if ( "buffer_size" in _params):
            linkif.addParam("input_buf_size",_params["buffer_size"])
            linkif.addParam("output_buf_size",_params["buffer_size"])
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(extraKeys))
        nic.addParam("id", nID)
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic", "rate":self.statInterval})
        return (nic, "rtr", _params["link_lat"])
        #print("Created Endpoint with id: %d, and params: %s %s\n"%(nID, _params.subset(self.nicKeys), _params.subset(extraKeys)))
    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;


class Pt2ptEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        self.epKeys.extend(["link_bw", "packet_size", "packets_to_send", "buffer_size", "src", "dest"])
        self.epOptKeys.extend(["linkcontrol","stream_delays","report_interval"])

    def getName(self):
        return "pt2pt Test End Point"

    def prepParams(self):
        #if "checkerboard" not in _params:
        #    _params["checkerboard"] = "1"
        pass

    def build(self, nID, extraKeys):
        nic = sst.Component("pt2ptNic.%d"%nID, "merlin.pt2pt_test")
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(extraKeys))
        nic.addParam("id", nID)
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic", "rate":self.statInterval})
        return (nic, "rtr", _params["link_lat"])
        #print("Created Endpoint with id: %d, and params: %s %s\n"%(nID, _params.subset(self.nicKeys), _params.subset(extraKeys)))

    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;


class OfferedLoadEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        self.epKeys.extend(["offered_load", "num_peers", "link_bw", "message_size", "buffer_size", "pattern"])
        self.epOptKeys.extend(["linkcontrol"])

    def getName(self):
        return "Offered Load End Point"

    def prepParams(self):
        pass

    def build(self, nID, extraKeys):
        nic = sst.Component("offered_load.%d"%nID, "merlin.offered_load")
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(extraKeys))
        nic.addParam("id", nID)
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic", "rate":self.statInterval})
        return (nic, "rtr", _params["link_lat"])
        #print("Created Endpoint with id: %d, and params: %s %s\n"%(nID, _params.subset(self.nicKeys), _params.subset(extraKeys)))

    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;



class ShiftEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        #self.nicKeys = ["topology", "num_peers", "num_messages", "link_bw", "checkerboard"]
        self.epKeys.extend(["num_peers", "link_bw", "shift"])
        self.epOptKeys.extend(["checkerboard", "packets_to_send", "packet_size"])

    def getName(self):
        return "Test End Point"

    def prepParams(self):
        #if "checkerboard" not in _params:
        #    _params["checkerboard"] = "1"
        #if "num_messages" not in _params:
        #    _params["num_messages"] = "10"
        pass

    def build(self, nID, extraKeys):
        nic = sst.Component("shiftNic.%d"%nID, "merlin.shift_nic")
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(extraKeys))
        nic.addParam("id", nID)
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic","rate":self.statInterval})
        return (nic, "rtr", _params["link_lat"])
        #print("Created Endpoint with id: %d, and params: %s %s\n"%(nID, _params.subset(self.nicKeys), _params.subset(extraKeys)))
    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;



class TrafficGenEndPoint(EndPoint):
    def __init__(self):
        EndPoint.__init__(self)
        #self.enableAllStats = False;
        #self.statInterval = "0"
        self.optionalKeys = ["delay_between_packets"]
        for genType in ["PacketDest", "PacketSize", "PacketDelay"]:
            for tag in ["pattern", "RangeMin", "RangeMax", "HotSpot:target", "HotSpot:targetProbability", "Normal:Mean", "Normal:Sigma", "Binomial:Mean", "Binomial:Sigma"]:
                self.optionalKeys.append("%s:%s"%(genType, tag))
        self.epKeys.extend(["topology", "num_peers", "link_bw", "packets_to_send", "packet_size", "message_rate", "PacketDest:pattern", "PacketDest:RangeMin", "PacketDest:RangeMax"])
        self.epOptKeys.extend("checkerboard")
        self.epOptKeys.extend(self.optionalKeys)
        self.nicKeys = []
    def getName(self):
        return "Pattern-based traffic generator"
    def prepParams(self):
        #if "checkerboard" not in _params:
        #    _params["checkerboard"] = "1"
        _params["PacketDest:RangeMin"] = 0
        _params["PacketDest:RangeMax"] = int(_params["num_peers"])

        if _params["PacketDest:pattern"] == "NearestNeighbor":
            self.nicKeys.append("PacketDest:NearestNeighbor:3DSize")
            if not "PacketDest:NearestNeighbor:3DSize" in _params:
                _params["PacketDest:NearestNeighbor:3DSize"] = "%s %s %s"%(_params["PacketDest:3D shape X"], _params["PacketDest:3D shape Y"], _params["PacketDest:3D shape Z"])
        elif _params["PacketDest:pattern"] == "HotSpot":
            self.nicKeys.append("PacketDest:HotSpot:target")
            self.nicKeys.append("PacketDest:HotSpot:targetProbability")
        elif _params["PacketDest:pattern"] == "Normal":
            self.nicKeys.append("PacketDest:Normal:Mean")
            self.nicKeys.append("PacketDest:Normal:Sigma")
        elif _params["PacketDest:pattern"] == "Binomial":
            self.nicKeys.append("PacketDest:Binomial:Mean")
            self.nicKeys.append("PacketDest:Binomial:Sigma")
        elif _params["PacketDest:pattern"] == "Exponential":
            self.nicKeys.append("PacketDest:Exponential:Lambda")
        elif _params["PacketDest:pattern"] == "Uniform":
            pass
        else:
            print("Unknown pattern" + _params["PacketDest:pattern"])
            sys.exit(1)

    def build(self, nID, extraKeys):
        nic = sst.Component("TrafficGen.%d"%nID, "merlin.trafficgen")
        linkif = nic.setSubComponent("networkIF","merlin.linkcontrol")
        if ( "link_bw" in _params):
            linkif.addParam("link_bw",_params["link_bw"])
        #if ( "input_buf_size" in _params):
        #    linkif.addParam("input_buf_size",_params["input_buf_size"])
        #if ( "output_buf_size" in _params):
        #    linkif.addParam("output_buf_size",_params["output_buf_size"])
        nic.addParams(_params.subset(self.epKeys, self.epOptKeys))
        nic.addParams(_params.subset(self.nicKeys))
        nic.addParams(_params.subset(extraKeys, {}))
        #for k in self.optionalKeys:
        #    if k in _params:
        #        nic.addParam(k, _params[k])
        nic.addParam("id", nID)
        if self.enableAllStats:
            nic.enableAllStatistics({"type":"sst.AccumulatorStatistic", "rate":self.statInterval})
        return (linkif, "rtr_port", _params["link_lat"])

    def enableAllStatistics(self,interval):
        self.enableAllStats = True;
        self.statInterval = interval;


############################################################################


if __name__ == "__main__":
    topos = dict([(1,topoTorus()), (2,topoFatTree()), (3,topoDragonFly()), (4,topoSimple())])
    endpoints = dict([(1,TestEndPoint()), (2, TrafficGenEndPoint()), (3, BisectionEndPoint())])


    print("Merlin SDL Generator\n")


    print("Please select network topology:")
    for (x,y) in topos.iteritems():
        print("[ %d ]  %s" % (x, y.getName() ))
    topo = int(input())
    if topo not in topos:
        print("Bad answer.  try again.")
        sys.exit(1)

    topo = topos[topo]



    print("Please select endpoint:")
    for (x,y) in endpoints.iteritems():
        print("[ %d ]  %s" % (x, y.getName() ))
    ep = int(input())
    if ep not in endpoints:
        print("Bad answer. try again.")
        sys.exit(1)

    endPoint = endpoints[ep];

    topo.prepParams()
    endPoint.prepParams()
    topo.setEndPoint(endPoint)
    topo.build()

