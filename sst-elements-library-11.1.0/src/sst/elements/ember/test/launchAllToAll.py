import re
import subprocess
from pathlib import Path
import sys
from argparse import ArgumentParser
import pathlib
import os
import time

topologies = {
  "hx": 1024,
  "hx2": 1024,
  "fattree": 1024,
  "fattree2": 1024,
  "torus": 1024,
  "dragonfly": 1024,
}

sizes = [1, 16, 256, 4096, 65536, 1048576]
#sizes = [44]

motif_folder = "loads"
output_folder = "output"
location_motif = motif_folder + "/" + "motifLoadAllToAll" # Change this to Path later
default_param_file = "defaultParams.py"

def check_if_exist():
    # First check if the folder we need to use exist
    path_motif = pathlib.Path(motif_folder)
    path_motif.mkdir(parents=True, exist_ok=True)
    path_out = pathlib.Path(output_folder)
    path_out.mkdir(parents=True, exist_ok=True)


def run_cluster(launch_string, size, name, topo):
    my_sbatch_cont = ["#!/bin/bash\n",
    "#SBATCH -N 8\n",
    "#SBATCH --time=03:50:00\n",
    "#SBATCH --mem=64G\n",
    "#SBATCH --cpus-per-task=4\n",
    "# Load the module environment suitable for the job\n" ,   
    "module load openmpi\n",
    "# And finally run the job\n"]

    my_sbatch_cont.append("srun --mem=64G -N 8 --cpus-per-task=4 time sstsim.x " + launch_string)
    # Write the array back to the same file
    file_name = "launch{}_{}_{}".format(topo, name, size)
    with open(file_name, 'w') as file:
        file.writelines(my_sbatch_cont)

    os.system("sbatch " + file_name)
    time.sleep(5)

def change_params(topo):
    new_lines = []
    with open(default_param_file, 'r') as f:
        for line in f:
            nic_bw = 400
            # Change topo size based on topology
            if (topo == "hx" or topo == "hx2" or topo == "torus"):
                nic_bw = 1600
            else:
                nic_bw = 400

            line = re.sub(r'"link_bw" : "\d+', '"link_bw" : "{}'.format(400), line)
            line = re.sub(r'"nic_link_bw" : "\d+', '"nic_link_bw" : "{}'.format(nic_bw), line)
            line = re.sub(r'"xbar_bw" : "\d+', '"xbar_bw" : "{}'.format(nic_bw), line)
            new_lines.append(line)

    # Write the array back to the same file
    with open(default_param_file, 'w') as file:
        file.writelines(new_lines)

def run_sst(topo, size, name = ""):
    change_params(topo)
    # Need to have a specific string for each topology
    output_fo = "output/" + name + "/"
    path_out = pathlib.Path(output_fo)
    path_out.mkdir(parents=True, exist_ok=True)
    if (topo == "hx"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hx --boardShape=4x4 --globalShape=8x8 --fatTreeShape=1:1,64 --hostsPerRtr=1 --loadFile={}" emberLoad.py > {}4hx{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    elif (topo == "hx2"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hx --boardShape=2x2 --globalShape=16x16 --fatTreeShape=1:1,64 --hostsPerRtr=1 --loadFile={}" emberLoad.py > {}2hx{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    elif (topo == "fattree"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=fattree --shape=32,32:32 --loadFile={}" emberLoad.py > {}fat_non{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    elif (topo == "fattree2"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=fattree --shape=32,16:32 --loadFile={}" emberLoad.py > {}fat_21{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    elif (topo == "torus"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=torus --shape=32x32 --hostsPerRtr=1 --loadFile={}" emberLoad.py > {}torus{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    elif (topo == "dragonfly"):
        launch_string = '--num_threads=4 --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=dragonfly --shape=32:1:16:32 --loadFile={}" emberLoad.py > {}dragonfly{}'.format(location_motif, output_fo, "_" + name + "_size" + str(size))
    else:
        print("Error, unknown topo")
        sys.exit(0)
    if (args.env == "local" or args.env == ""):
        process = subprocess.Popen(["time sst " + launch_string], shell=True)
        process.wait()
    else:
        run_cluster(launch_string, size, name, topo)

def main(args):
    for msg_size in sizes:
        for topo in topologies:
            if (args.topo != "" and args.topo != topo):
                continue
            print(topo)
            # Parse Current File, replace data and store it in an array
            new_lines = []
            generating_id_string = ""
            with open(location_motif, 'r') as f:
                for line in f:
                    # Change topo size based on topology
                    if (topo == "dragonfly"):
                        generating_id_string = "[NID_LIST] generateNidList=generateNidListRange(0,1024)\n"
                    elif (topo != "hx" and topo != "hx2"):
                        generating_id_string = "[NID_LIST] generateNidList=generateNidListRange(0,1024)\n"
                    elif (topo == "hx"):
                       generating_id_string = "[NID_LIST] generateNidList=generateNidListHx(4x4x8x8)\n"
                    elif (topo == "hx2"):
                        generating_id_string = "[NID_LIST] generateNidList=generateNidListHx(2x2x16x16)\n"
            
                    line = re.sub(r'bytes=\d+', "bytes={}".format(msg_size), line)
                    new_lines.append(line)

            # Write the array back to the same file
            with open(location_motif, 'w') as file:
                print(generating_id_string)
                new_lines[1] = generating_id_string
                file.writelines(new_lines)

            run_sst(topo, msg_size, "AllToAll")
    

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--topo", type=str, help="Topology to run", default="")
    parser.add_argument("--env", type=str, help="Local or Cluster", default="")
    args = parser.parse_args()
    check_if_exist()
    main(args)