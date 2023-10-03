
include_directories(/mnt/d/llvm-project/llvm/include/)

add_library(LICM SHARED LICM.cpp RegAllocIntfGraph.cpp)
