# llama.cpp, the engine that runs Pods' on-device AI models, built into the app (CPU only, no network code).
# Pinned to one release, so every platform builds the same thing.
include(FetchContent)
set(LLAMA_TAG b12100 CACHE STRING "llama.cpp release Pods' AI is built with")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(opt LLAMA_BUILD_COMMON LLAMA_BUILD_TESTS LLAMA_BUILD_TOOLS LLAMA_BUILD_EXAMPLES LLAMA_BUILD_SERVER
            LLAMA_BUILD_APP LLAMA_BUILD_UI LLAMA_CONNECT LLAMA_OPENSSL LLAMA_CURL LLAMA_TOOLS_INSTALL LLAMA_TESTS_INSTALL
            GGML_OPENMP GGML_NATIVE GGML_BACKEND_DL GGML_CPU_ALL_VARIANTS)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
# Computers: the instructions every PC made since about 2013 has (AVX2). Phones: plain ARMv8, which every phone runs.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND NOT ANDROID)
  foreach(opt GGML_SSE42 GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C)
    set(${opt} ON CACHE BOOL "" FORCE)
  endforeach()
  set(GGML_BMI2 OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(llama
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG ${LLAMA_TAG}
  GIT_SHALLOW TRUE
  GIT_PROGRESS FALSE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(llama)
