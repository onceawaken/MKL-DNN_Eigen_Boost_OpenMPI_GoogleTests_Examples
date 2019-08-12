//
// Created by egrzrbr on 2019-04-28.
//

#ifndef NONBLOCKINGPROTOCOL_MPI_TESTS_H
#define NONBLOCKINGPROTOCOL_MPI_TESTS_H

#include <string>
#include <iostream>
#include <stdio.h>
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>

#ifdef WITH_MPI

#include <mpi.h>

#endif

#ifdef WITH_BOOST_MPI

#include <boost/mpi.hpp>
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>
#include <boost/serialization/string.hpp>
#include <boost/numeric/ublas/vector.hpp>
#include <boost/mpi/collectives/gather.hpp>

namespace mpi = boost::mpi;
namespace mt  = mpi::threading;

#endif




int randi_range(int min, int max) //range : [min, max)
{

	srand(time(NULL)); //seeding for the first time only!
	return min + rand() % ((max + 1) - min);

}

template<typename ... Args>
std::string string_format(const std::string &format, Args ... args) {

	size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}


enum msg_e {
	BEFORE_DATA_E,
	COMPUTING_E,
	AFTER_DATA_E,
	SENDING_E,
	RECEIVING_E,

};

enum tag_e {
	TAG_REQUEST_E,
	TAG_IRECV_E

};

enum msg_flag_e {

	BEGIN_E = INT_MIN,
	END_E = INT_MAX,

};

template<typename T, size_t N, typename std::enable_if_t<(N > 1e5), T> * = nullptr>
auto make_arr() {

	return std::vector<T>(N, 0);

}

template<typename T, size_t N, typename std::enable_if_t<(N <= 1e5), T> * = nullptr>
auto make_arr() {

	return std::array<T, N>();

}


template<typename T, class Iter>
auto vec_sum(Iter begin, Iter end) {

	return std::accumulate(begin, end, T{0}, [](T in, T x) { return in + x; });
	// every process sums it’s chunk
}

#endif //NONBLOCKINGPROTOCOL_MPI_TESTS_H
