
SET(sfgpu_SOURCES
	${CMAKE_CURRENT_LIST_DIR}/sf_gpu.cu
)

CUDA_INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include)
CUDA_ADD_LIBRARY(sfgpu ${sfgpu_SOURCES})
