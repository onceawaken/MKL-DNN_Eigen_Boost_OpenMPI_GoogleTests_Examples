//
// Created by egrzrbr on 2019-05-01.
//

#ifndef NONBLOCKINGPROTOCOL_MPI_GLOBAL_LOCAL_RANKS_HXX
#define NONBLOCKINGPROTOCOL_MPI_GLOBAL_LOCAL_RANKS_HXX

int test_boost_mpi_local_global_rank(mpi::environment &env, mpi::communicator &world) {


	MPI_Comm shmcomm;
	MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0,
	                    MPI_INFO_NULL, &shmcomm);
	int shmrank;
	MPI_Comm_rank(shmcomm, &shmrank);

	std::cout << "I am process " << world.rank() << "/" << world.size() << ", shmcomm=" << shmcomm << ", shmrank=" << shmrank << "." << std::endl;


	return 0;
}

#endif //NONBLOCKINGPROTOCOL_MPI_GLOBAL_LOCAL_RANKS_HXX
