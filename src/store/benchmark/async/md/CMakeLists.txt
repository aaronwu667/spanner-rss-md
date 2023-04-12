file(GLOB SourceFiles "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(BenchmarkMD ${SourceFiles})
target_include_directories(BenchmarkMD PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
target_link_libraries(BenchmarkMD BenchmarkCommon StoreCommonFrontend)
