//
// Created by egrzrbr on 2019-05-01.
//

template<typename T, size_t N>
int irecv_request(MPI_Request &sreq, MPI_Status &sprobe, MPI_Message &msg, int rank) {

	auto input = make_arr<T, N + 4>();
	MPI_Imrecv(&input[0], input.size(), MPI_INT, &msg, &sreq);
	printf("       [ %i/%i ] <<<<< : RECV data { STATUS[SRC:%i, TAG:%i, ERR:%i], DATA[ %i | %i | %i | %i ] }\n",
	       sprobe.MPI_SOURCE, rank, sprobe.MPI_SOURCE, sprobe.MPI_TAG, sprobe.MPI_ERROR,
	       input[0], input[1], input[N + 3 - 1], input[N + 4 - 1]);
}


int probe_irecv_request(MPI_Request &sreq, int &fprobes, MPI_Status &sprobe, MPI_Message &msg, int rank, int sender) {

	int fprobe = 0;

	int err = MPI_Improbe(sender, TAG_REQUEST_E, MPI_COMM_WORLD, &fprobe, &msg, &sprobe);
	if (fprobe) {
		printf("       [ %i/%i ] <---- : probe recv { true, TAG[%i] } \n", sender, rank, TAG_REQUEST_E);

		fprobes += 1;
	} else {
		printf("       [ %i/%i ] <---- : probe recv { false, TAG[%i] } \n", sender, rank, TAG_REQUEST_E);
	}

	return fprobe;
}

void test_isend_request(MPI_Request &sreq, int &ftests, MPI_Status &stest, int rank, int target) {
	/*
	 Use: MPI_Request_get_status - that will preserve request status when request is complete.
	 Reason: MPI_Test - that will clean request when its complete (flag ftest is set to true).
	*/

	int ftest = 0;
	MPI_Request_get_status(sreq, &ftest, &stest);
	if (ftest) {
		ftests += 1;
		printf(" >>>>> [ %i/%i ]       : SEND data { STATUS[SRC:%i, TAG:%i, ERR:%i] }\n",
		       rank, target, stest.MPI_SOURCE, stest.MPI_TAG, stest.MPI_ERROR);
		MPI_Test(&sreq, &ftest, &stest);
	} else {
		printf(" ----> [ %i/%i ]       : test send { TAG[%i] } \n", rank, target, TAG_REQUEST_E);
	}

}

void mpi_nonblocking_p2p(int argc, char *argv[]) {

	MPI_Init(&argc, &argv);
	int rank, nproc;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);

	constexpr size_t N = (size_t) 1e8;

	auto value = make_arr<int, N + 4>();

	std::fill(std::begin(value) + 2, std::end(value) - 2, rank + 1);
	int checksum = std::accumulate(std::begin(value) + 2, std::end(value) - 2, int{0});

	value[0] = BEGIN_E;
	value[1] = checksum;
	value[N + 3 - 1] = checksum;
	value[N + 4 - 1] = END_E;

	int next = (rank + 1) % nproc;
	int prev = (rank + nproc - 1) % nproc;
	MPI_Request sreq;
	MPI_Isend(&value[0], value.size(), MPI_INT, next, TAG_REQUEST_E, MPI_COMM_WORLD, &sreq);
	int fprobe = 0;
	int ftest = 0;
	MPI_Message msg;
	MPI_Status stest, sprobe;
	do {
		if (!ftest) {
			MPI_Test(&sreq, &ftest, &stest);
			if (ftest) {
				printf("Isend : [ %i<%i> >>> %i ]\n", rank, TAG_REQUEST_E, next);
			} else {
				printf("Test  : [ %i<%i> --> %i ]\n", rank, TAG_REQUEST_E, next);
			}
		}
		if (!fprobe) {
			int err = MPI_Improbe(prev, TAG_REQUEST_E, MPI_COMM_WORLD, &fprobe, &msg, &sprobe);
			if (fprobe) {
				auto input = make_arr<int, N + 4>();
				MPI_Imrecv(&input[0], input.size(), MPI_INT, &msg, &sreq);
				printf("Irecv : [ %i<%i> <<< %i ] : RCV input { PROBE[SRC:%i, TAG:%i, ERR:%i], DATA[ %i | %i | %i | %i ] }\n",
				       rank, TAG_REQUEST_E, prev, sprobe.MPI_SOURCE, sprobe.MPI_TAG, sprobe.MPI_ERROR,
				       input[0], input[1], input[N + 3 - 1], input[N + 4 - 1]);
			} else {
				printf("Probe : [ %i<%i> <-- %i ]\n", rank, TAG_REQUEST_E, prev);
			}
		}
	} while (fprobe == 0 || ftest == 0);
	MPI_Finalize();
}


void mpi_nonblocking_p2p_all_to_all(int argc, char **argv) {

	MPI_Init(&argc, &argv);
	int rank, nproc;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);

	constexpr size_t N = (size_t) 1e8;

	std::cout << "size [MB] : " << sizeof(int) * N / (1024 * 1024) << std::endl;

	auto value = make_arr<int, N + 4>();

	std::fill(std::begin(value) + 2, std::end(value) - 2, rank + 1);
	int checksum = std::accumulate(std::begin(value) + 2, std::end(value) - 2, int{0});

	value[0] = BEGIN_E;
	value[1] = checksum;
	value[N + 3 - 1] = checksum;
	value[N + 4 - 1] = END_E;

	MPI_Request sreq[32]{};
	for (int proc = 0; proc < nproc; proc++) {
		MPI_Isend(&value[0], value.size(), MPI_INT, proc, TAG_REQUEST_E, MPI_COMM_WORLD, &sreq[proc]);
	}

	int fprobe = 0;
	int fprobes = 0;
	int ftests = 0;

	MPI_Message msg;
	MPI_Status stest, sprobe;
	do {
		if (ftests < nproc) {
			for (int i = 0; i < nproc; i++) {
				test_isend_request(sreq[i], ftests, stest, rank, i);
			}
		}

		if (fprobes < nproc) {
			for (int i = 0; i < nproc; i++) {
				fprobe = probe_irecv_request(sreq[i], fprobes, sprobe, msg, rank, i);
				if (fprobe) {
					irecv_request<int, N>(sreq[i], sprobe, msg, rank);
				}
			}
		}
	} while (fprobes < nproc || ftests < nproc);

	MPI_Finalize();

}
