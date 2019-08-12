//
// Created by egrzrbr on 2019-05-01.
//

#include "mpi_tests.h"


int test_boost_mpi_broadcast(mpi::environment &env, mpi::communicator &world) {

	const int rank = world.rank();
	const bool isMaster = world.rank() == 0;

	std::string value[4];

	if (isMaster) {
		value[0] = "Hello, World!";
	} else {
		value[rank] = std::to_string(rank);
	}

	for (int i = 0; i < 4; i++) {
		broadcast(world, value[i], i);

		std::cout << "Process #" << world.rank() << " says " << value[i] << std::endl;
	}

	return 0;
}

int test_boost_mpi_master_gather_value(mpi::environment &env, mpi::communicator &world) {

	std::cout << world.size() << std::endl;

	int rank = world.rank();
	int isMaster = rank == 0;

	int value = rank;

	if (isMaster) {
		std::vector<int> all_numbers;

		gather(world, value, all_numbers, 0);

		for (int proc = 0; proc < world.size(); ++proc)
			std::cout << "Process #" << proc << " thought of "
			          << all_numbers[proc] << std::endl;

	} else {
		gather(world, value, 0);
	}

	return 0;
}