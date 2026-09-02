def generate( args ):
    net_info = ""
    if "_" in args: # Job size specified
        net_info = args.split("_")[0]
    else:
        net_info = args
    net_info = net_info.split('x')
    rows_board = int(net_info[0]) # Rows within a board
    cols_board = int(net_info[1]) # Cols within a board
    rows = int(net_info[2]) # Rows of HxMesh
    cols = int(net_info[3]) # Cols of HxMesh

    if "_" in args: # Job size specified
        job_rows = int(args.split("_")[1].split("x")[0])
        job_cols = int(args.split("_")[1].split("x")[1])
    else:
        job_rows = rows*rows_board
        job_cols = cols*cols_board
    

    # Generate IDs
    #ids = [[0 for x in range(cols*cols_board)] for y in range(rows*rows_board)] # Create a rows*rows_board*cols*cols_board matrix
    nids = job_rows*job_cols
    pos = 0
    ret = '' 
    new_id = 0
    for i in range(rows*rows_board):
        for j in range(cols*cols_board):
            board_row = int(int(new_id / (cols*cols_board))/rows_board)
            board_col = int((new_id % (cols*cols_board))/cols_board)
            board_id = int((board_row*cols) + board_col)
            #print(str(board_row) + "," + str(board_col) + "->" + str(board_id))
            # This board contains ids from 
            board_start_id = board_id * (rows_board*cols_board) # First ID within the board
            row_in_board = int(new_id / (cols*cols_board)) % rows_board
            col_in_board = int(new_id % (cols*cols_board)) % cols_board            
            id = board_start_id + row_in_board*cols_board + col_in_board
            new_id += 1

            if i < job_rows and j < job_cols:
                ret = ret + str(int(id))
                pos += 1            
                if pos < nids:
                    ret = ret + ','
    #print(ret)
    return ret
