//
// Created by egrzrbr on 2019-04-28.
//

#include "mpi_tests.h"

#include "mpi_non_blocking_p2p.hxx"

#include "mpi_global_local_ranks.hxx"

#include "mpi_non_blocking_bcast.hxx"

/*
 * Client-Centric Consistency Models
   - These models assume that clients connect to different replicas at each time
   - The models ensure that whenever a client connects to a replica,
     the replica is bought up to date with the replica that the client accessed previously
 */


int main(int argc, char *argv[]) {

	//mpi_nonblocking_all_to_all(argc, argv);

	//mpi_nonblocking_broadcast_all_to_all(argc, argv);

	//test_ibcast();

	test_ibcast_all_to_all();

	return 0;

	mpi::environment env;
	mpi::communicator world;


	//test_boost_mpi_broadcast(env, world);
	//test_boost_mpi_local_global_rank(env, world);

}