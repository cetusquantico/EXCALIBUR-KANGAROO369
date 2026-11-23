cmake_minimum_required(VERSION 3.10)
project(EXCALIBUR-KANGAROO369 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops")

find_package(Threads REQUIRED)

add_library(kangaroo_ec STATIC src/ec.cpp src/kangaroo.cpp src/hash.cpp src/checkpoint.cpp)
target_include_directories(kangaroo_ec PUBLIC src)
target_link_libraries(kangaroo_ec PUBLIC Threads::Threads)

add_executable(excalibur src/main.cpp)
target_link_libraries(excalibur kangaroo_ec)

add_executable(test_ec tests/test_ec.cpp)
target_link_libraries(test_ec kangaroo_ec)

add_executable(bench_batch tests/bench_batch.cpp)
target_link_libraries(bench_batch kangaroo_ec)

add_executable(test_kangaroo tests/test_kangaroo.cpp)
target_link_libraries(test_kangaroo kangaroo_ec)

add_executable(test_sym tests/test_sym.cpp)
target_link_libraries(test_sym kangaroo_ec)

add_executable(test_real tests/test_real.cpp)
target_link_libraries(test_real kangaroo_ec)
configure_file(${CMAKE_SOURCE_DIR}/tests/puzzles.csv ${CMAKE_BINARY_DIR}/puzzles.csv COPYONLY)

add_executable(test_wide tests/test_wide.cpp)
target_link_libraries(test_wide kangaroo_ec)

add_executable(test_ckpt tests/test_ckpt.cpp)
target_link_libraries(test_ckpt kangaroo_ec)

add_executable(test_sym_high tests/test_sym_high.cpp)
target_link_libraries(test_sym_high kangaroo_ec)

enable_testing()
add_test(NAME ec_vectors COMMAND test_ec)
add_test(NAME batch_correctness COMMAND bench_batch)
add_test(NAME kangaroo_solve COMMAND test_kangaroo)
add_test(NAME kangaroo_symmetry COMMAND test_sym)
add_test(NAME real_puzzles COMMAND test_real)
add_test(NAME wide_160bit COMMAND test_wide)
add_test(NAME checkpoint_resume COMMAND test_ckpt)
add_test(NAME symmetry_54bit COMMAND test_sym_high)
