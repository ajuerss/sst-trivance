#!/bin/bash
set -e

# ============================================================
# Main paper
# ============================================================

# Figure 6: 8-node ring
python3 launchAll.py --topo torus --job_size 8 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 7: 64-node ring
python3 launchAll.py --topo torus --job_size 64 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 8: 32x32 torus, default bandwidth of 800 Gb/s
python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 9: rectangular 8x64 torus
python3 launchAll.py --topo torus --job_size 8x64 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB


# ============================================================
# Figure 10: bandwidth evaluation on 32x32 torus
#
# 800 Gb/s is already generated above for Figure 8.
# Additional bandwidths are 200, 400, 1600, 2400, and 3200 Gb/s.
# ============================================================

python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB --netBW 200Gb/s

python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB --netBW 400Gb/s

python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB --netBW 1600Gb/s

python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB --netBW 2400Gb/s

python3 launchAll.py --topo torus --job_size 32x32 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB --netBW 3200Gb/s


# Figure 11: power-of-three 27x27 torus
# Recursive Doubling is omitted in the paper for power-of-three sizes.
python3 launchAll.py --topo torus --job_size 27x27 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,BruckL,BruckB

# Figure 12: 16x16x16 torus
python3 launchAll.py --topo torus --job_size 16x16x16 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB


# ============================================================
# Appendix G: Additional Plots
# ============================================================

# Figure 16: 128-node ring
python3 launchAll.py --topo torus --job_size 128 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 17: 8x8 torus
python3 launchAll.py --topo torus --job_size 8x8 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 18: 8x16 torus
python3 launchAll.py --topo torus --job_size 8x16 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 19: 16x64 torus
python3 launchAll.py --topo torus --job_size 16x64 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB

# Figure 20: 8x8x8 torus
python3 launchAll.py --topo torus --job_size 8x8x8 --bench Bucket,TrivanceL,TrivanceB,SwingL,SwingB,RecDoubL,RecDoubB,BruckL,BruckB
