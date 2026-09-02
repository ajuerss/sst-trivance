# Trivance AllReduce Simulator

This repository contains the simulator and experiment infrastructure used to evaluate **Trivance**, an AllReduce algorithm for multiport direct-connect networks.

The simulator relies on **SST Core 11.1.0 and SST Elements 11.1.0**. We rely on the original implementation for Swing and the launch scripts on Daniele De Sensi from the paper "Swing: Short-cutting Rings for Higher Bandwidth Allreduce".(https://github.com/HLC-Lab/swing-allreduce-sim)


The artifact has been tested on **macOS** as well as on **Ubuntu22.04** and has been verified to build correctly. Instructions for macOS and Linux are provided below.

---

## SST Version

This simulator specifically relies on:

```text
SST Core 11.1.0
SST Elements 11.1.0
```

The SST source trees required by the simulator are included in this repository:

```text
sstcore-11.1.0/
sst-elements-library-11.1.0/
```

The artifact should therefore be built using the included **11.1.0 versions**. The collective implementations and experiment infrastructure in this repository were developed and tested against this version of SST.

---

## Repository Structure

The main files after the build, relevant to the Trivance artifact are:

```text
.
├── benchmarks/
│   ├── general_bench.py
│   └── allreduce/
│       ├── launchAll.py
│       ├── launch_all.sh
│       ├── output/
│       └── ...
│
├── sstcore-11.1.0/
│
├── src/
│   └── sstcore-11.1.0.tar.gz
│
├── sst-elements-library-11.1.0/
│   └── src/sst/elements/ember/
│       ├── Makefile.am
│       ├── mpi/motifs/
│       │   ├── embertrivancecoll.cc
│       │   ├── embertrivancecoll.h
│       │   ├── embertrivancereducescatter.cc
│       │   ├── embertrivancereducescatter.h
│       │   ├── embertrivanceallgather.cc
│       │   ├── embertrivanceallgather.h
│       │   ├── embertrivanceallreduce.cc
│       │   ├── embertrivanceallreduce.h
│       │   ├── emberbruck...
│       │   ├── emberswing...
│       │   └── emberrecdoub...
│       └── test/
│           ├── defaultParams.py
│           ├── defaultParams1D.py
│           ├── defaultParams2D.py
│           ├── defaultParams3D.py
│           ├── defaultParams4D.py
│           └── emberLoad.py
│
└── requirements.txt
```

The files prefixed with `embertrivance` contain the Trivance implementation added for this work.

---

## Installation and Build

The simulator uses the included **SST Core 11.1.0** and **SST Elements 11.1.0** sources. SST Core must be built first, followed by SST Elements. The setup below follows the environment used for the experiments.

The commands assume that this repository is located at:

```text
HOME/sst-trivance
```

If the repository is located elsewhere, change `$HOME/sst-trivance` accordingly.

### 1. System Dependencies

On **macOS**, install the Xcode command-line tools and the required build tools:

```bash
xcode-select --install
brew install autoconf automake libtool wget python
```

On **Ubuntu/Linux**:

```bash
sudo apt-get update
sudo apt-get install -y build-essential autoconf automake libtool wget python3 python3-pip python3-setuptools
```

### 2. Build OpenMPI 4.0.5

The following steps are identical on macOS and Linux:

```bash
mkdir -p $HOME/sst-trivance/packages

cd $HOME/sst-trivance/src

wget https://download.open-mpi.org/release/open-mpi/v4.0/openmpi-4.0.5.tar.gz
tar xfz openmpi-4.0.5.tar.gz

cd openmpi-4.0.5

mkdir -p $HOME/sst-trivance/packages/OpenMPI-4.0.5

export MPIHOME=$HOME/sst-trivance/packages/OpenMPI-4.0.5

./configure --prefix=$MPIHOME --disable-mpi-fortran

make all install

export PATH=$MPIHOME/bin:$PATH
export MPICC=mpicc
export MPICXX=mpicxx
export PMIX_MCA_gds=hash
export MANPATH=$MPIHOME/share/man:${MANPATH:-}
```
We disable fortran as it is not required for SST and prone to issues.


On **Linux**, additionally use:

```bash
export LD_LIBRARY_PATH=$MPIHOME/lib:${LD_LIBRARY_PATH:-}
```

On **macOS**, use:

```bash
export DYLD_LIBRARY_PATH=$MPIHOME/lib:${DYLD_LIBRARY_PATH:-}
```

The OpenMPI installation can be checked using:

```bash
which mpicc
mpicc --version
```

### 3. Build SST Core 11.1.0

**Get the source from the release tarball.** Copying existing source tree may lose file timestamps and the executable bit on `configure` leading to issues.

```bash
cd $HOME/sst-trivance/src
tar xzf sstcore-11.1.0.tar.gz
cd sstcore-11.1.0
```

`tar xzf` preserves mtimes and permissions, so no `chmod` is needed.

```bash
export SST_CORE_HOME=$HOME/sst-trivance/sstcore-11.1.0
export SST_CORE_ROOT=$HOME/sst-trivance/src/sstcore-11.1.0

./configure --prefix="$SST_CORE_HOME" --disable-dependency-tracking

make all
make install

export PATH=$SST_CORE_HOME/bin:$PATH
```

`--disable-dependency-tracking` is required on macOS: automake's depfiles
bootstrap fails against Xcode's GNU make 3.81. You lose only incremental
rebuild-on-header-change.

Check the installation:

```bash
which sst
sst --version
sst-info
sst-test-core
```

`sst --version` should report SST Core 11.1.0.

#### Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `config.status: error: Something went wrong bootstrapping makefile fragments` | depfiles bootstrap on macOS | add `--disable-dependency-tracking` |
| `./configure: Permission denied` | exec bit lost during copy | re-extract from the tarball |
| `configure.ac:9: error: version mismatch ... Automake 1.18.1` | scrambled timestamps trigger autotools regeneration | re-extract from the tarball |
| `fatal error: 'sst/core/env/envquery.h' file not found` | incomplete source tree | re-extract from the tarball |

If `make all` fails inside the compile itself (missing `<cstdint>`/`<cstring>`
includes, `-Wall -Wextra` errors), you're hitting a 2021 codebase against modern
Apple clang. Building in a container (Ubuntu 20.04 + gcc 9) is usually faster
than patching sources, and keeps the evaluation environment reproducible.

### 4. Build SST Elements 11.1.0

SST Elements is built against the SST Core installation from the previous step.

```bash
cd $HOME/sst-trivance/sst-elements-library-11.1.0

export SST_ELEMENTS_HOME=$HOME/sst-trivance/sst-elements-library-11.1.0
export SST_ELEMENTS_ROOT=$HOME/sst-trivance/sst-elements-library-11.1.0

aclocal
autoconf
autoheader
automake --add-missing

./configure --prefix="$SST_ELEMENTS_HOME" --with-sst-core="$SST_CORE_HOME"

make all
make install

export PATH=$SST_ELEMENTS_HOME/bin:$PATH
```

The `aclocal`, `autoconf`, `autoheader`, and `automake` steps are required because this artifact adds new Ember collective source files through `src/sst/elements/ember/Makefile.am`.

The SST Elements installation can be checked with:

```bash
sst-info
```

### 5. Python Dependencies

From the repository root:

```bash
cd $HOME/sst-trivance

python3 -m pip install -U pip setuptools wheel
python3 -m pip install -r requirements.txt
```

The Ember configuration directory must be available through `PYTHONPATH`:

```bash
export PYTHONPATH=$HOME/sst-trivance/sst-elements-library-11.1.0/src/sst/elements/ember/test:${PYTHONPATH:-}
```

### 6. Environment After Reopening the Terminal

The `export` commands above only affect the current terminal session. The software itself does **not** need to be rebuilt after closing the terminal.

On **macOS**, add the environment variables to `~/.zshrc`:

```bash

export MPIHOME=$HOME/sst-trivance/packages/OpenMPI-4.0.5
export SST_CORE_HOME=$HOME/sst-trivance/sstcore-11.1.0
export SST_CORE_ROOT=$HOME/sst-trivance/src/sstcore-11.1.0
export SST_ELEMENTS_ROOT=$HOME/sst-trivance/sst-elements-library-11.1.0
export SST_ELEMENTS_ROOT=$HOME/sst-trivance/sst-elements-library-11.1.0

export PATH=$MPIHOME/bin:$SST_CORE_HOME/bin:$SST_ELEMENTS_HOME/bin:$PATH

export MPICC=mpicc
export MPICXX=mpicxx
export PMIX_MCA_gds=hash

export DYLD_LIBRARY_PATH=$MPIHOME/lib:${DYLD_LIBRARY_PATH:-}
export PYTHONPATH=$HOME/sst-trivance/sst-elements-library-11.1.0/src/sst/elements/ember/test:${PYTHONPATH:-}
```

Reload it using:

```bash
source ~/.zshrc
```

On **Linux**, add the same variables to `~/.bashrc`, but use:

```bash
export LD_LIBRARY_PATH=$MPIHOME/lib:${LD_LIBRARY_PATH:-}
```

instead of `DYLD_LIBRARY_PATH`.

Reload it using:

```bash
source ~/.bashrc
```

After opening a new terminal, the setup can be checked with:

```bash
which mpicc
which sst
sst --version
```

### 7. Rebuilding After Code Changes

If an existing collective `.cc` or `.h` file is modified, only SST Elements needs to be rebuilt:

```bash
cd $SST_ELEMENTS_ROOT
make all
make install
```

SST Core does not need to be rebuilt.

If a **new collective source file is added**, also add it to:

```text
sst-elements-library-11.1.0/src/sst/elements/ember/Makefile.am
```

and regenerate the build files before rebuilding:

```bash
cd $SST_ELEMENTS_ROOT

aclocal
autoconf
autoheader
automake --add-missing

./configure \
    --prefix="$SST_ELEMENTS_HOME" \
    --with-sst-core="$SST_CORE_HOME"

make all
make install
```

Changes only to experiment scripts or Python configuration files do not require rebuilding SST.

### 8. Test run to verify functionality

To verify if all components of the SST build were created correctly, execute the test command to run AllReduce for Trivance, Recursive Doubling and Bruck for a 1D torus network of 8 nodes:

```bash
bash $HOME/sst-trivance/benchmarks/allreduce/launch_test.sh
```

The result should be written in output for each algorithm consisting of per step byte transfers and a valid completion time in the nano- or microseconds.

---

## Running the AllReduce Experiments

The AllReduce experiments are located in:

```text
benchmarks/allreduce/
```

The main experiment launcher is:

```text
benchmarks/allreduce/launchAll.py
```

and the main experiment configurations are provided in:

```text
benchmarks/allreduce/launch_all.sh
```

These scripts are based on the **original Swing experiment scripts by Daniele De Sensi** and have been extended with the Trivance and Bruck configurations used in this artifact.

Move to the experiment directory:

```bash
cd $HOME/sst-trivance/benchmarks/allreduce
```

For example, Trivance can be evaluated on a `16 x 16 x 16` torus using:

```bash
python3 launchAll.py \
    --topo torus \
    --job_size 16x16x16 \
    --bench TrivanceL,TrivanceB
```

Alternatively, the experiment commands collected in:

```text
launch_all.sh
```

can be executed using:

```bash
bash launch_all.sh
```

### Expected Runtime

Running the complete set of experiments takes approximately **36 hours** on the machine used for our local evaluation:

- MacBook Pro
- 2.6 GHz 6-Core Intel Core i7
- 16 GB RAM

The runtime can be reduced significantly when using a more powerful machine or VM with additional CPU cores and sufficient memory. Individual experiments and the provided functionality test complete substantially faster than the full experiment suite. The results should remain identical as the simulator is deterministic.

---

## Collective Implementations

The collective implementations are located in:

```text
sst-elements-library-11.1.0/src/sst/elements/ember/mpi/motifs/
```

The versions contained in this repository include the modifications used for the Trivance evaluation.

Other existing SST/Ember collective implementations, including recursive doubling and ring-based algorithms, are used as additional baselines where applicable. We extended collective implementations to incur per step delay and extend them for multidimensional torus networks.

---

## Modifying a Collective

Collective source code is located under:

```text
sst-elements-library-11.1.0/src/sst/elements/ember/mpi/motifs/
```

For example, the main Trivance implementation can be modified in:

```text
embertrivancecoll.cc
embertrivanceallreduce.cc
embertrivancereducescatter.cc
embertrivanceallgather.cc
```

If only an existing `.cc` or `.h` file is modified, rebuild and reinstall SST Elements:

```bash
cd $SST_ELEMENTS_ROOT

make all
make install
```

SST Core does not need to be rebuilt when only a collective implementation in SST Elements is changed.

If a **new collective source file is added**, it must additionally be registered in:

```text
sst-elements-library-11.1.0/src/sst/elements/ember/Makefile.am
```

After changing `Makefile.am`, regenerate the SST Elements build files:

```bash
cd $SST_ELEMENTS_ROOT

aclocal
autoconf
autoheader
automake --add-missing
```

Then configure and rebuild SST Elements:

```bash
./configure \
    --prefix=$SST_ELEMENTS_HOME \
    --with-sst-core=$SST_CORE_HOME

make all
make install
```

---

## Experiment Parameters

The main experiment parameters are controlled through:

```bash
python3 benchmarks/allreduce/launchAll.py
```

The launcher is derived from the **Swing experiment launcher by Daniele De Sensi** and has been extended with Trivance-specific configurations.

Available parameters can be displayed using:

```bash
python3 launchAll.py --help
```

For example:

```bash
python3 launchAll.py \
    --topo torus \
    --job_size 32x32 \
    --bench TrivanceL,TrivanceB \
    --counts 2^13,2^15,2^17 \
    --netBW 800Gb/s
```

The principal parameters are:

```text
--topo         network topology
--job_size     dimensions of the simulated network/job
--bench        collective implementation(s)
--counts       collective message counts
--netBW        link bandwidth
--num_threads  number of SST simulation threads
```

Additional network parameters include:

```text
--netPktSize
--netFlitSize
--linkLat
--nicBW
```

Changing these experiment-level parameters does **not** require rebuilding SST.

---

## Experiment Output and Plot Generation

### Simulation Output

Running `launchAll.py` automatically creates the required output directories below:

```text
benchmarks/allreduce/output/
```

Results are first organized by the simulated torus size and then by collective configuration. The `L` and `B` suffixes distinguish the latency- and bandwidth-optimal versions of an algorithm, respectively.

For example, experiments for an 8-node torus produce a structure of the form:

```text
output/
└── torus_8/
    ├── TrivanceL/
    ├── TrivanceB/
    ├── SwingL/
    ├── SwingB/
    ├── RecDoubL/
    ├── RecDoubB/
    ├── BruckL/
    ├── BruckB/
    └── Bucket/
```

Within each algorithm directory, the individual result files are separated by the message count used for the experiment. For example, the bandwidth-optimal Trivance experiment with `count=512` on 8 nodes is written to:

```text
output/torus_8/TrivanceB/512
```

If a non-default bandwidth is selected, the bandwidth is included in the topology directory name. For example, experiments using `200Gb/s` are written below a directory with the suffix:

```text
_200Gb
```

A typical result file contains the complete SST/Ember configuration, the communication performed in each collective step, timing statistics, and the final simulated execution time. For example:

```text
EMBER: network: topology=torus shape=8
EMBER: numNodes=8 numNics=8
link_bw 800Gb/s
EMBER: network: BW=800Gb/s pktSize=8192B flitSize=256B
EMBER: Motif='TrivanceAllreduce count=512 ports=2 dimensions=1 dimensions_sizes=8 px=8 latency_optimal=0 aggregation_cost_ns=0 blocking=true sync=true'

COMM step=0 port=0 from=6 to=7 bytes=768
COMM step=0 port=1 from=6 to=5 bytes=512
...
TIME 0 0 8847 8847 2048 3584 3.240873 25.926981
STATS 10 0 EMBER 0 8847 8847 3584
...
Simulation is complete, simulated time: 13.5181 us
```

The `COMM` lines describe the communication performed by the collective, including the step, port, sender, receiver, and number of transmitted bytes. The `TIME` and `STATS` lines contain the timing and communication statistics used for processing the experiment results. The final `Simulation is complete` line indicates that the SST simulation completed successfully.

### Main Result Plots

Generated plots are stored under:

```text
benchmarks/allreduce/output/plots/
```

The main relative-performance plot is generated using:

```bash
cd $HOME/sst-trivance/benchmarks/allreduce
python3 plot_relative.py
```

All parameters required by `plot_relative.py` are currently configured directly in the script. The script reads the corresponding simulation results from `output/` and writes the generated plots below `output/plots/`.

### Bandwidth Plots

Bandwidth-specific results use output directories whose names contain the corresponding bandwidth suffix. For example, simulations executed with `200Gb/s` use directories containing:

```text
_200Gb
```

The bandwidth plots are generated from these results and written to:

```text
output/plots/bw/
```

The bandwidth plotting script can be executed with:

```bash
python3 plot_bw.py
```

### Sensitivity Analysis

The sensitivity plots are generated with `plot_sensitivity.py`. The script reads the simulation results from the `output/` directory and writes the generated PDF figures to:

```text
output/plots/sens/
```

A sensitivity mode must be specified using the `--mode` argument to read then the file from a specific folder of that mode like "link_lat", "pktsize" etc. (details can be changed in the script). For example, the bandwidth sensitivity plot can be generated with:

```bash
python3 plot_sensitivity.py --mode bw
```

Supported modes are:

```text
bw
packet
flit
nic
linklat
step
```

By default, the script evaluates the `32x32` torus configuration. A different topology size can be selected using the `--shape` argument. For example:

```bash
python3 plot_sensitivity.py --mode bw --shape 8x8
```

The sensitivity modes correspond to variations of the network bandwidth, packet size, flit size, NIC bandwidth, link latency, and per-step delay, respectively.
