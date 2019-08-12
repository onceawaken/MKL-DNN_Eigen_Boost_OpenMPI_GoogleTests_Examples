

#find_package(mkldnn CONFIG REQUIRED HINTS ${MKL_DNN_LIB_PATH}/cmake/mkldnn)

project(MKL_DNN_Example_Simple_Net_Cpp LANGUAGES C CXX)
cmake_minimum_required(VERSION 3.13)
ppr_blue("Build EXECUTABLE: ${PROJECT_NAME}...")


add_executable_mkl_dnn(${PROJECT_NAME} simple_net.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS ON)


config_bin_output(${PROJECT_NAME})


project(MKL_DNN_Example_Simple_Training_Net LANGUAGES C CXX)
cmake_minimum_required(VERSION 3.13)
ppr_blue("Build EXECUTABLE: ${PROJECT_NAME}...")


add_executable_mkl_dnn(${PROJECT_NAME} simple_training_net.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS ON)


config_bin_output(${PROJECT_NAME})
