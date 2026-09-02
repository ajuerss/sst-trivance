import math

# Returns the bandwidth term
def getBSquare(D, t, v, n, beta, relsteps = 0):
    con = (1/(2*D))
    acc = 0
    delta_sum = 0
    #print("--")
    r = 0
    for s in range(t, t + v):    
        # Compute delta
        delta = 0
        if relsteps:
            sigma = relsteps
        else:
            sigma = s // D
        for i  in range(0, sigma + 1):
            delta += (-2)**i     
        delta = abs(delta)
        #print(delta)
        delta_sum += delta
        ##
        acc += (delta)/(2**(s+1))
        r += 1
        if relsteps and r % D == 0:
            relsteps += 1
    #print(delta_sum)
    return con*acc*n*beta

def getBNonSquare(dimensions, n, beta):
    dimensions.sort() # They must be ordered from smallest to biggest
    D = len(dimensions)
    bw_overhead = 0
    i = 0
    steps_so_far = 0
    relative_steps_so_far = 0
    for d in dimensions:    
        if i == 0:
            start = 0
            steps = (D-i)*(math.log2(d))
        else:
            start = steps_so_far
            steps = (D-i)*(math.log2(d) - math.log2(dimensions[i - 1]))
        if start > math.log2(p) - 1 or steps == 0:
            steps_so_far += steps    
            continue           
        #print("steps " + str(steps) + " start " + str(start) + " relsofar " + str(relative_steps_so_far))
        bw_overhead += getBSquare(D - i, int(start), int(steps), n, beta, relative_steps_so_far)
        relative_steps_so_far = int(math.log2(d))
        #n /= (2**(steps + 1))
        steps_so_far += steps 
        i += 1
    return bw_overhead

beta = 0.00002 # 400Gbps
alpha = 0.35 # us
dimensions = [16, 32]
#dimensions = [8, 16]
p = 1
for d in dimensions:
    p *= d
D = len(dimensions)
Dmax = max(dimensions)
Dmin = min(dimensions)

for n in [1024, 32768, 1048576, 16777216, 134217728]:
    overhead_bucket = 1 #((Dmax - 1)/Dmax)*(Dmin / (Dmin - 1))
    
    if Dmax != Dmin:
        bw_swing = getBNonSquare(dimensions, n, beta)    
    else:
        bw_swing = getBSquare(D, 0, int((math.log2(p))), n, beta)

    model_tree = math.log2(p)*alpha + math.log2(p)*(n/(2*D))*beta
    model_bucket = D*(Dmax-1)*alpha + (n/(2*D))*beta*overhead_bucket
    model_hamiltonian = p*alpha + (n/(2*D))*beta
    model_flattened = p*alpha + getBSquare(1, 0, int((math.log2(p))), n, beta) # No need to divide by 2*D because it is already done by getB functions
    model_swing = math.log2(p)*alpha + bw_swing # No need to divide by 2*D because it is already done by getB functions
    model_swing_approx = math.log2(p)*alpha + (getBSquare(D, 0, Dmin**(D), n, beta)) + (getBSquare(1, 0, Dmax, n/(2**D), beta))  # No need to divide by 2*D because it is already done by getB functions    
    model_swing_lb_better = math.log2(p)*alpha + (getBSquare(D, 0, int(math.log2(Dmin)*(D)), n, beta)) + ((n*beta)/(6*2**((D-0)*math.log(Dmin, 2))))*(math.log(2*Dmin, 2) - math.log(Dmin, 2))*Dmin  # No need to divide by 2*D because it is already done by getB functions
    model_swing_ub_better = math.log2(p)*alpha + (getBSquare(D, 0, int(math.log2(Dmin)*(D)), n, beta)) + ((n*beta)/(6*2**((D-0)*math.log(Dmin, 2))))*(math.log(Dmax, 2) - math.log(Dmin, 2))*Dmin  # No need to divide by 2*D because it is already done by getB functions
    model_swing_ub_better_simplified = math.log2(p)*alpha + (getBSquare(D, 0, int(math.log2(Dmin)*(D)), n, beta)) + (n*beta*math.log2(Dmax/Dmin))/(6*(Dmin**(D-1)))  # No need to divide by 2*D because it is already done by getB functions
    model_swing_lb_better_simplified = math.log2(p)*alpha + (getBSquare(D, 0, int(math.log2(Dmin)*(D)), n, beta)) + (n*beta)/(6*(Dmin**(D-1)))  # No need to divide by 2*D because it is already done by getB functions
    assert model_swing_lb_better == model_swing_lb_better_simplified
    assert model_swing_ub_better == model_swing_ub_better_simplified
    #assert model_swing >= model_swing_lb
    #assert model_swing <= model_swing_ub
    #assert model_swing_ub_better <= model_swing_ub
    #assert model_swing_lb_better >= model_swing_lb
    #assert model_swing <= model_swing_ub_better 
    #assert model_swing >= model_swing_lb_better 
    
    print(f"{n} Tree: {model_tree} Flat: {model_flattened} Hamiltonian: {model_hamiltonian} Bucket: {model_bucket} Swing: {model_swing} Swing lb better : {model_swing_lb_better} Swing ub better: {model_swing_ub_better}")
