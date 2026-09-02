#!/bin/bash

run_sst() {
    key=$1
    algo=$2
    board_shape=$3
    global_shape=$4
    px=$5
    count=$6
    aggcost=$7
    blocking=$8

    shape_str="${board_shape}x${global_shape}"
    cat allreduceMotif.template | sed -e "s/#ALGO#/$algo/g"         \
                                      -e "s/#SHAPE#/$shape_str/g"   \
                                      -e "s/#PX#/$px/g"             \
                                      -e "s/#COUNT#/$count/g"       \
                                      -e "s/#AGGCOST#/$aggcost/g"   \
                                      -e "s/#BLOCKING#/$blocking/g" > allreduceMotif

    sst --num_threads=1                                     \
        --model-options="                                   \
           --param="nic:module=merlin.reorderlinkcontrol"   \
           --topo=hx                                        \
           --boardShape=$board_shape                        \
           --globalShape=$global_shape                      \
           --fatTreeShape=1                                 \
           --hostsPerRtr=1                                  \
           --loadFile=allreduceMotif"                       \
        emberLoad.py > out

    key_fname=$(echo $key | sed "s/ /./g")
    cat out | grep "TIME" | sed "s/\(.*\)/$key \1/g" > data/run.${key_fname}.out
}

agg_cost=0
blocking="false"
for count in 512 8192 131072 2097152 33554432; do
    for mesh_size in 2; do
        for global_size in 1 2 4; do
            board_shape="${mesh_size}x${mesh_size}"
            global_shape="${global_size}x${global_size}"
            px=$(echo "${mesh_size}*${global_size}" | bc)
 
            algo="RingAllreduce2D"
            key="$mesh_size $mesh_size $global_size $global_size $count $algo $blocking $px $agg_cost"
    
            echo $key    
            run_sst "$key" "$algo" "$board_shape" "$global_shape" "$px" "$count" "$agg_cost" "$blocking"

        done
    done
done
