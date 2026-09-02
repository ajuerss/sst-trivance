def generate( args ):
    net_info = ""
    if "_" in args: # Job size specified
        net_info = args.split("_")[0]
    else:
        net_info = args
    net_info = net_info.split('x')
    rows_board = int(net_info[0]) # Rows within a board
    cols_board = int(net_info[1]) # Cols within a board

    if "_" in args: # Job size specified
        job_rows = int(args.split("_")[1].split("x")[0])
        job_cols = int(args.split("_")[1].split("x")[1])
    else:
        print("not valid torus allocation")
        exit(0)

    # Generate IDs
    #ids = [[0 for x in range(cols*cols_board)] for y in range(rows*rows_board)] # Create a rows*rows_board*cols*cols_board matrix
    nids = job_rows*job_cols
    pos = 0
    ret = '' 
    new_id = 0
    my_id = 0
    for i in range(rows_board):
        col_idx = 0
        my_id = i * cols_board
        if (i >= job_rows):
            continue
        for j in range(cols_board):
            if (j < job_cols):
                ret = ret + str(int(my_id)) + ","
            col_idx += 1
            my_id += 1

    ret = ret[:-1]
    #print(ret)
    return ret
