project(Boost_Example VERSION 1.0.0 LANGUAGES C CXX)


if (BOOST_FOUND)
    ppr_blue("Build EXE: ${PROJECT_NAME}_Matmul")

    set(SOURCES
            matmul_boost.cpp
            )

    set(BOOST_LIBS
            )

    add_executable_boost(${PROJECT_NAME}_Matmul "${SOURCES}" "${BOOST_LIBS}")

    #remove debug info
    #     -DNDEBUG -DBOOST_UBLAS_NDEBUG

    ppr_blue("Build PROJECT (exe samples): ${PROJECT_NAME}")

    set(SOURCES
            main.cpp
            )

    set(BOOST_LIBS
            filesystem
            )

    add_executable_boost(${PROJECT_NAME}_Filesystem "${SOURCES}" "${BOOST_LIBS}")

    set(SOURCES
            main.regex.cpp
            )

    set(BOOST_LIBS
            regex
            )

    add_executable_boost(${PROJECT_NAME}_Regex "${SOURCES}" "${BOOST_LIBS}")


endif ()


