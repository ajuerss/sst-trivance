import subprocess
import sys
from argparse import ArgumentParser
import shutil
import os
import time

# Change this to Path later
logs_folder = "logs"
sbatch_folder = "sbatch"
ember_load_folder = os.path.join(
    os.path.expanduser("~"),
    "sst-trivance",
    "sst-elements-library-11.1.0",
    "src",
    "sst",
    "elements",
    "ember",
    "test",
) + os.sep
sstsim_path = os.path.join(os.environ["SST_CORE_HOME"], "libexec", "sstsim.x")


def allocate_logic(bench, topo):
    # We allocate only for GPT or Cosmo, not needed for other benchmarks
    if ("DLRM" in bench and topo == "hx4"):
        return "SST_NO_MEM=1 IS_XH4=1"
    if ("gpt" in bench.lower() or "cosmo" in bench.lower()):
        return "SST_NO_MEM=0"
    else:
        return "SST_NO_MEM=1"


def run_slim(args, launch_string, bench, topo):
    if (topo == "dragonfly" and args.nodes > 4):
        node_num = 128
    else:
        node_num = args.nodes

    allocation_policy = allocate_logic(bench, topo)

    slim_string = " time /scratch/2/t2hx/dep/adaptive_openmpi/bin/mpirun -x {} -mca plm_rsh_no_tree_spawn 1 --map-by node -mca btl openib,self,sm -mca btl_openib_if_include mlx4_0 --hostfile {} -mca orte_base_help_aggregate 0  -np {} {}".format(
        allocation_policy,
        args.hostfile,
        node_num,
        sstsim_path
    )

    print(slim_string + " " + launch_string)
    os.system(slim_string + " " + launch_string)


def run_cluster(args, launch_string, size, bench, topo):

    # Special case if we are running on SlimFly
    if (args.env == "slimfly"):
        return run_slim(args, launch_string, bench, topo)

    allocation_policy = allocate_logic(bench, topo)

    my_sbatch_cont = [
        "#!/bin/bash\n",
        "#SBATCH -N {}\n".format(args.nodes),
        "#SBATCH -n {}\n".format(args.nodes),
        "#SBATCH --time=03:59:59\n",
        "#SBATCH -A g34\n",
        "#SBATCH --mem={}\n".format(args.mem),
        "#SBATCH --cpus-per-task={}\n".format(args.cpus_per_task),
        "#SBATCH -C mc\n",
        "#SBATCH --output=logs/slurm-%A.out\n",
        "# Load the module environment suitable for the job\n",
        "module load openmpi\n",
        "{}\n".format(allocation_policy),
        "# And finally run the job\n"
    ]

    if (args.env == "ault"):
        my_sbatch_cont = [
            x for x in my_sbatch_cont
            if x != "#SBATCH -C mc\n"
        ]
        my_sbatch_cont = [
            x for x in my_sbatch_cont
            if x != "#SBATCH -A g34\n"
        ]

    my_sbatch_cont.append(
        "SST_NO_MEM=1 srun --mem={} -N {} -n {} --cpus-per-task={} sstsim.x ".format(
            args.mem,
            args.nodes,
            args.nodes,
            args.cpus_per_task
        ) + launch_string
    )

    # Write the array back to the same file
    file_name = "launch{}_{}_{}".format(topo, bench, size)

    with open(sbatch_folder + "/" + file_name, 'w') as file:
        file.writelines(my_sbatch_cont)

    os.system("sbatch " + sbatch_folder + "/" + file_name)
    time.sleep(2)


def run_sst(args, topo, count, bench, shape, motif_file, out_file):

    ember_load = ember_load_folder + "emberLoad.py"

    dimensions = 1

    if (topo == "hx4"):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hx --boardShape=4x4 --globalShape={} --fatTreeShape=1:1,64 --hostsPerRtr=1 --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )
        dimensions = 2

    elif (topo == "hx2"):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hx --boardShape=2x2 --globalShape={} --fatTreeShape=1:1,64 --hostsPerRtr=1 --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )
        dimensions = 2

    elif (topo == "hx1"):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hx --boardShape=1x1 --globalShape={} --fatTreeShape=1:1,64 --hostsPerRtr=1 --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )
        dimensions = 2

    elif (topo == "hyperx"):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=hyperx --shape={} --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )
        dimensions = 2

    elif ("fattree" in topo):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=fattree --shape={} --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )

    elif (topo == "torus"):
        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=torus --shape={} --hostsPerRtr=1 --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )
        dimensions = shape.count("x") + 1

    elif (topo == "dragonfly"):

        # Special case for Dragonfly
        if args.num_threads > 8:
            args.num_threads = 8

        launch_string = '--num_threads={} --model-options="--param="nic:module=merlin.reorderlinkcontrol" --topo=dragonfly --shape={} --netBW={} --loadFile={}" {} > {}'.format(
            str(args.num_threads),
            shape,
            args.netBW,
            motif_file,
            ember_load,
            out_file
        )

    else:
        print("Error, unknown topo")
        sys.exit(0)

    # Set defaultParams
    defaultParamsName = "defaultParams" + str(dimensions) + "D.py"

    shutil.copyfile(
        ember_load_folder + defaultParamsName,
        ember_load_folder + "defaultParams.py"
    )

    if (args.env == "local" or args.env == ""):
        allocation_policy = allocate_logic(bench, topo)

        print("{} {} ".format(
            allocation_policy,
            sstsim_path
        ) + launch_string)

        process = subprocess.Popen(
            ["{} {} ".format(
                allocation_policy,
                sstsim_path
            ) + launch_string],
            shell=True
        )

        process.wait()

    elif (
        args.env == "slurm"
        or args.env == "cluster"
        or args.env == "ault"
        or args.env == "slimfly"
        or args.env == "daint"
    ):
        run_cluster(args, launch_string, count, bench, topo)

    else:
        print("Error, unknown env")
        sys.exit(0)
