

if (OMPI_FOUND)


    project(OpenMPI_Tests CXX)

    set(TARGET ${PROJECT_NAME})

    ppr_blue("Build EXE: ${TARGET}")

    include_directories(CppUtils)

    set(SOURCES
            mpi_tests.cpp mpi_tests.h
            )

    set(MPI_NP 4)

    add_executable_mpi(${TARGET} ${MPI_NP} ${SOURCES})

    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG=4 OPAL_PREFIX=$ENV{OPAL_PREFIX} LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH})


    # Boost MPI


    project(OpenMPI_Tests_Boost CXX)

    set(TARGET ${PROJECT_NAME})

    ppr_blue("Build EXE: ${TARGET}")

    set(BOOST_LIBS
            threading
            )

    set(SOURCES
            mpi_tests.cpp mpi_tests.h
            )

    add_executable_boost_ompi(${TARGET} ${MPI_NP} "${SOURCES}" ${BOOST_LIBS})


endif ()

