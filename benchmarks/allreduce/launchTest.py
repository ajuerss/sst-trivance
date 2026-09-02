import sys
from argparse import ArgumentParser
import pathlib
import numpy as np
sys.path.append("../")
from general_bench import *

bench_to_motif = {}
bench_to_motif["Rings"] = "RingAllreduce25D"
bench_to_motif["Bucket"] = "RingAllreduceRev"
bench_to_motif["SwingB"] = "SwingAllreduce"
bench_to_motif["SwingL"] = "SwingAllreduce"
bench_to_motif["RecDoubB"] = "RecDoubAllreduce"
bench_to_motif["RecDoubL"] = "RecDoubAllreduce"
bench_to_motif["TrivanceB"] = "TrivanceAllreduce"
bench_to_motif["TrivanceL"] = "TrivanceAllreduce"
bench_to_motif["BruckB"] = "BruckAllreduce"
bench_to_motif["BruckL"] = "BruckAllreduce"

motif_folder = os.getcwd() + "/loads"
out_folder = os.getcwd() + "/output"

def check_if_exist(subpath):
    # First check if the folder we need to use exist
    path_motif = pathlib.Path(motif_folder + "/" + subpath)
    path_motif.mkdir(parents=True, exist_ok=True)
    path_out = pathlib.Path(out_folder + "/" + subpath)
    path_out.mkdir(parents=True, exist_ok=True)
    path_sbatch = pathlib.Path(sbatch_folder)
    path_sbatch.mkdir(parents=True, exist_ok=True)
    path_logs = pathlib.Path(logs_folder)
    path_logs.mkdir(parents=True, exist_ok=True)

def generate_simulations(args, bench):
    allreduce_counts_all = [2**7, 2**9, 2**11]

    allreduce_counts_small = [2**7, 2**9, 2**11]

    if args.counts != "All":
        countstmp = args.counts.split(",")
        counts = []
        for c in countstmp:
            counts += [int(c.split("^")[0]) ** int(c.split("^")[1])]
    elif bench == "SwingL" or bench == "RecDoubL" or bench == "TrivanceL" or bench == "BruckL":
        counts = allreduce_counts_small
    else:
        counts = allreduce_counts_all
    
    dimensions_sizes = ','.join(args.job_size.split("x"))
    dimensions = args.job_size.count("x") + 1
    
    shape = args.job_size

    print(dimensions_sizes)

    for count in counts:
        nnodes = np.prod([int(x) for x in args.job_size.split("x")])
        generateNidList = "generateNidListRange(0,{})".format(nnodes)
        bw = ""
        if args.netBW != "800Gb/s":
            bw = "_" + args.netBW.split("/")[0]
        subpath = "torus" + "_" + shape + bw + "/" + bench
        check_if_exist(subpath)
        output = out_folder + "/" + subpath + "/" + str(count)

        if bench == "SwingL" or bench == "RecDoubL" or bench == "TrivanceL" or bench == "BruckL":
            latency_optimal = "1"
        else:
            latency_optimal = "0"

        if bench == "SwingL" or bench == "RecDoubL":
            ports = "1"
        else:
            ports = str(int(dimensions) * 2)
        motif_name = bench_to_motif[bench]
        if bench == "Rings" and int(dimensions) == 1:
            motif_name = "RingAllreduce05D"

        motif_content = ["[JOB_ID] 10\n",
                        "[NID_LIST] generateNidList={}\n".format(generateNidList),
                        "[MOTIF] Init\n",
                        "[MOTIF] {} count={} ports={} dimensions={} dimensions_sizes={} px={} latency_optimal={} aggregation_cost_ns=0 blocking=true sync=true\n".format(motif_name, count, ports, dimensions, dimensions_sizes, dimensions_sizes.split(",")[-1], latency_optimal),
                        "[MOTIF] Fini"]
        
        motif_file = motif_folder + "/" + subpath + "/" + str(count)
        with open(motif_file, 'w') as outfile:
            outfile.writelines(motif_content)
        run_sst(args, "torus", count, bench, shape, motif_file, output)

def main(args):
    benchmarks = ["SwingB", "SwingL", "RecDoubB", "RecDoubL", "Bucket", "TrivanceB", "TrivanceL", "BruckB", "BruckL"]
    if args.bench != "All":
        benchmarks = args.bench.split(",")
    for bench in benchmarks:
        dimensions = args.job_size.count("x") + 1
        if dimensions > 2 and bench == "Rings":
            continue
        """
        if dimensions == 1 and bench == "Bucket":
            continue
        """
        generate_simulations(args, bench)

if __name__ == "__main__":
    parser = ArgumentParser()
    parser.add_argument("--topo", type=str, help="Topology to run", default="", choices=["torus"])
    parser.add_argument("--num_threads", type=int, help="Number of threads to use for SST", default=8)
    parser.add_argument("--env", type=str, help="Local or Cluster", default="", choices=["cluster", "daint", "ault", "local", "slimfly"])
    parser.add_argument("--nodes", type=int, help="Number of nodes for cluster", default="8")
    parser.add_argument("--cpus_per_task", type=str, help="Number of cores per node for cluster", default="8")
    parser.add_argument("--mem", type=str, help="Memory per Node for cluster", default="16G")
    parser.add_argument("--hostfile", type=str, help="Hostfile name for Slimfly", default="hostfile")
    parser.add_argument("--job_size", type=str, help="Size of the job", default="8x8")
    parser.add_argument("--bench", type=str, help="Benchmark to run", default="All")
    parser.add_argument("--counts", type=str, help="Counts", default="All")
    parser.add_argument("--netBW", type=str, help="Link bandwidth", default="800Gb/s")
    parser.add_argument("--netPktSize", type=str, default="")
    parser.add_argument("--netFlitSize", type=str, default="")
    parser.add_argument("--linkLat", type=str, default="")
    parser.add_argument("--nicBW", type=str, default="")
    args = parser.parse_args()
    main(args)
