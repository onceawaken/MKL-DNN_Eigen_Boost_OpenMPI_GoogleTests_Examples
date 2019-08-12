//
// Created by egrzrbr on 2019-04-28.
//

#include "mpi_tests.h"

int test_boost_mpi_omp() {

	mpi::environment env(mt::funneled);
	if (env.thread_level() < mt::funneled) {
		env.abort(-1);
	}


	
	mpi::communicator world;
	std::cout << "I am process " << world.rank() << " of " << world.size()
	<< ", thread : " << env.thread_level() << " (main thread : " << env.is_main_thread() << ")." << std::endl;
	return 0;
}


