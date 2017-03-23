set(LLVM_LINK_COMPONENTS support)

add_clang_executable(clang-mapper
  ClangMapper.cpp 
  CallGraphAction.cpp 
  CallGraphAction.h 
  CallGraph.cpp
  CallGraph.h
  Commons.h
  )
target_link_libraries(clang-mapper
  clangTooling
  clangBasic
  clangASTMatchers
  )
