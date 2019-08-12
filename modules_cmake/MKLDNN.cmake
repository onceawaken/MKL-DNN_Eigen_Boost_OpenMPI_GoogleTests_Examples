  

function(assert_mkl_dnn)
    message("MKL_DNN_FOUND=${MKL_DNN_FOUND}")
    if (NOT MKL_DNN_FOUND)
        message(FATAL_ERROR "ERROR: MKL_DNN is not found")
    endif ()
endfunction()

macro(setup_mkl_dnn)
    unset(MKL_DNN_FOUND CACHE)

    if (DEFINED ARGN1)
        set(MKL_DNN_DIR ${ARGN1})
    else ()
        set(MKL_DNN_DIR ${CMAKE_LIBRARY_PATH}/MKL_DNN/${CMAKE_BUILD_TYPE})
    endif ()

    message("MKL_DNN_DIR = ${MKL_DNN_DIR}")

    set(MKL_DNN_LIBS mkldnn mklml_intel iomp5)
    set(MKL_DNN_LIB_PATH ${MKL_DNN_DIR}/lib)
    set(MKL_DNN_INCLUDE_PATH ${MKL_DNN_DIR}/include)

    set(ENV{LD_LIBRARY_PATH} "$ENV{LD_LIBRARY_PATH}:${MKL_DNN_LIB_PATH}")

    MESSAGE(">>>ENV{OPAL_PREFIX}=$ENV{OPAL_PREFIX}")
    MESSAGE(">>>ENV{LD_LIBRARY_PATH}=$ENV{LD_LIBRARY_PATH}")

    option(MKL_DNN_FOUND "Set flag that MKL_DNN is found now" ON)

    message("MKL_DNN_FOUND=${MKL_DNN_FOUND}")
endmacro()

macro(add_executable_mkl_dnn TARGET)

    set(extra_macro_args ${ARGN})

    list(LENGTH extra_macro_args num_extra_args)
    if (${num_extra_args} GREATER 0)
        set(SOURCES ${ARGN})
    endif ()

    ppr_blue("add_executable_MKL_DNN : TARGET=${TARGET}; SOURCES=${SOURCES}...")

    assert_mkl_dnn()

    if (NOT DEFINED PROJECT_NAME)
        message("MKL_DNN project not defined. Set PROJECT(${TARGET})...")
        PROJECT(${TARGET})
    endif ()


    add_executable(${TARGET} ${SOURCES})



    # Attach directories
    target_link_directories(${TARGET} PRIVATE ${MKL_DNN_LIB_PATH})
    target_include_directories(${TARGET} PRIVATE ${MKL_DNN_INCLUDE_PATH})
    target_link_libraries(${TARGET} PRIVATE ${MKL_DNN_LIBS} m)
        

    message(">>> MKL_DNN_LIBS : ${MKL_DNN_LIBS}")
    message(">>> LD_LIBRARY_PATH : $ENV{LD_LIBRARY_PATH}")

    set_target_properties(${TARGET} PROPERTIES FOLDER ${CMAKE_LIBRARY_PATH})

    target_compile_definitions(${TARGET} PRIVATE
            OPAL_PREFIX=$ENV{OPAL_PREFIX}
            LD_LIBRARY_PATH=$ENV{LD_LIBRARY_PATH}
            WITH_MKL_DNN=1
            )

    config_bin_output(${TARGET})

    target_copy_extern_libs(${TARGET} ${MKL_DNN_LIB_PATH} ${MKL_DNN_LIBS})


    configure_file(${CMAKE_SOURCE_DIR}/src/bin/run.sh.in ${RUNTIME_OUTPUT_DIR}/${TARGET}.sh)

endmacro()


