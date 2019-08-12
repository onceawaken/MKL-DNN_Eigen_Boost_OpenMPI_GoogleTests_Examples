//
// Created by egrzrbr on 2019-05-01.
//

#ifndef NONBLOCKINGPROTOCOL_MPI_NON_BLOCKING_BCAST_HXX
#define NONBLOCKINGPROTOCOL_MPI_NON_BLOCKING_BCAST_HXX

void test_ibcast_send_request(MPI_Request &req, int &flag, int &flags, MPI_Status &status, int rank) {
	/*
	 Use: MPI_Request_get_status - that will preserve request status when request is complete.
	 Reason: MPI_Test - that will clean request when its complete (flag is set to true).
	*/

	int err = MPI_Request_get_status(req, &flag, &status);
	if (flag) {
		printf(" >>>>> [ %i/%i | %i ]       : SEND { STATUS[SRC:%i, TAG:%i, ERR:%i] }\n",
		       rank, status.MPI_SOURCE, flags, status.MPI_SOURCE, status.MPI_TAG, status.MPI_ERROR);
		MPI_Test(&req, &flag, &status);
		flags++;
	} else {
		//printf(" ----> [ %i/ALL ]       : test send { TAG[%i] } \n", rank, TAG_REQUEST_E);
	}

}


template<class container_t, size_t N>
int test_ibcast_recv_request(MPI_Request &req, int &flag, int &flags, MPI_Status &status, int rank, int sender, const container_t &input) {

	int err = MPI_Request_get_status(req, &flag, &status);
	if (flag) {
		printf("       [ %i/%i | %i ] <<<<< : RECV { STATUS[SRC:%i, TAG:%i, ERR:%i], DATA[ %i | %i | %i | %i ] }\n",
		       rank, sender, flags, status.MPI_SOURCE, status.MPI_TAG, status.MPI_ERROR,
		       input[0], input[1], input[N + 3 - 1], input[N + 4 - 1]

		);
		MPI_Test(&req, &flag, &status);
		flags++;
	} else {
		//printf("       [ %i/%i ] <---- : test recv { false, TAG[%i] } \n", sender, rank, TAG_REQUEST_E);
	}

	return flag;
}


int test_ibcast() {

	int rank, size;
	int data[(int) 1e5];

	MPI_Init(NULL, NULL);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	printf("MPI start %d/%d\n", rank, size);

	int flags = 0;

	if (rank == 0) {
		sleep(1);
		std::fill(std::begin(data), std::end(data), 1);
		printf("MPI %d/%d bcast\n", rank, size);
		MPI_Request req;
		MPI_Status status;
		MPI_Ibcast(&data, 1e5, MPI_INT, 0, MPI_COMM_WORLD, &req);

		int flag = 0;
		while (flag == 0) {
			test_ibcast_send_request(req, flag, flags, status, 0);
		}
		std::cout << "After wait" << std::endl;

	} else {
		int flag = 0;
		MPI_Request req = MPI_REQUEST_NULL;
		MPI_Status status;
		MPI_Ibcast(&data, 1e5, MPI_INT, 0, MPI_COMM_WORLD, &req);

		while (flag == 0) {
			test_ibcast_recv_request<decltype(data), (int) 1e5>(req, flag, flags, status, rank, 0, data);
			usleep(100 * 1000);
		}
		// MPI_Bcast can be done!
		//MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);
		printf("MPI %d/%d recv bcast data: %d\n", rank, size, data[0]);
	}
	MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}


int test_ibcast_all_to_all() {

	constexpr size_t N = (int) 1e5;

	int rank, size;
	int data[(int) N + 4];
	int recv[10][N + 4];

	MPI_Init(NULL, NULL);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	printf("MPI start %d/%d\n", rank, size);

	sleep(1);
	std::fill(std::begin(data) + 2, std::end(data) - 2, rank + 1);

	data[0] = INT_MIN;
	data[1] = vec_sum<int>(std::begin(data) + 2, std::end(data) - 2);
	data[N + 3 - 1] = data[1];
	data[N + 4 - 1] = INT_MAX;

	MPI_Request reqSend;
	MPI_Status sSend;
	MPI_Status sRecv[10];

	auto reqRecv = std::unordered_map<int, MPI_Request>(size);
	for (int i = 0; i < size; i++) {
		reqRecv[i] = MPI_REQUEST_NULL;
	}

	for (auto &p : reqRecv) {
		int i = p.first;
		if (i == rank)
			MPI_Ibcast(&data[0], N + 4, MPI_INT, i, MPI_COMM_WORLD, &p.second);
		else
			MPI_Ibcast(&recv[i][0], N + 4, MPI_INT, i, MPI_COMM_WORLD, &p.second);
	}

	int ftests = 0;
	do {

		for (auto &p : reqRecv) {
			int i = p.first;
			if (p.second != MPI_REQUEST_NULL) {
				int fprobe = 0;
				if (i == rank) {
					test_ibcast_send_request(p.second, fprobe, ftests, sRecv[i], rank);
				} else {
					test_ibcast_recv_request<decltype(recv[i]), N>(p.second, fprobe, ftests, sRecv[i], rank, i, recv[i]);
				}
				if (fprobe) {
					reqRecv[i] = MPI_REQUEST_NULL;
				}
			}
		}

		for (auto &p : reqRecv) {
			if (p.second == MPI_REQUEST_NULL) {
				reqRecv.erase(p.first);
			}
		}

	} while (not reqRecv.empty());


	// MPI_Bcast can be done!
	//MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);

	MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}


#endif //NONBLOCKINGPROTOCOL_MPI_NON_BLOCKING_BCAST_HXX
