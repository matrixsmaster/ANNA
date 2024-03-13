#CC=clang-14
#CXX=clang++-14
LLAMA_CUBLAS=1
#LLAMA_DEBUG=1


# Define the default target now so that it is always the first target
BUILD_TARGETS = libanna.a anna anna_server

all: $(BUILD_TARGETS)
.PHONY: all

ifndef UNAME_S
UNAME_S := $(shell uname -s)
endif

ifndef UNAME_P
UNAME_P := $(shell uname -p)
endif

ifndef UNAME_M
UNAME_M := $(shell uname -m)
endif

#
# Compile flags
#

# keep standard at C11 and C++11
MK_CPPFLAGS = -I. -Icommon
MK_CFLAGS   = -std=c11   -fPIC
MK_CXXFLAGS = -std=c++11 -fPIC

# -Ofast tends to produce faster code, but may not be available for some compilers.
ifndef LLAMA_DEBUG
	MK_CFLAGS     += -Ofast
	HOST_CXXFLAGS += -Ofast
	MK_NVCCFLAGS  += -O3
endif

# clock_gettime came in POSIX.1b (1993)
# CLOCK_MONOTONIC came in POSIX.1-2001 / SUSv3 as optional
# posix_memalign came in POSIX.1-2001 / SUSv3
# M_PI is an XSI extension since POSIX.1-2001 / SUSv3, came in XPG1 (1985)
MK_CPPFLAGS += -D_XOPEN_SOURCE=600

# Somehow in OpenBSD whenever POSIX conformance is specified
# some string functions rely on locale_t availability,
# which was introduced in POSIX.1-2008, forcing us to go higher
ifeq ($(UNAME_S),OpenBSD)
	MK_CPPFLAGS += -U_XOPEN_SOURCE -D_XOPEN_SOURCE=700
endif

# Data types, macros and functions related to controlling CPU affinity and
# some memory allocation are available on Linux through GNU extensions in libc
ifeq ($(UNAME_S),Linux)
	MK_CPPFLAGS += -D_GNU_SOURCE
	# Use mmap on *nix
	MK_CPPFLAGS += -DANNA_USE_MMAP
endif

# RLIMIT_MEMLOCK came in BSD, is not specified in POSIX.1,
# and on macOS its availability depends on enabling Darwin extensions
# similarly on DragonFly, enabling BSD extensions is necessary
ifeq ($(UNAME_S),Darwin)
	MK_CPPFLAGS += -D_DARWIN_C_SOURCE
endif
ifeq ($(UNAME_S),DragonFly)
	MK_CPPFLAGS += -D__BSD_VISIBLE
endif

# alloca is a non-standard interface that is not visible on BSDs when
# POSIX conformance is specified, but not all of them provide a clean way
# to enable it in such cases
ifeq ($(UNAME_S),FreeBSD)
	MK_CPPFLAGS += -D__BSD_VISIBLE
endif
ifeq ($(UNAME_S),NetBSD)
	MK_CPPFLAGS += -D_NETBSD_SOURCE
endif
ifeq ($(UNAME_S),OpenBSD)
	MK_CPPFLAGS += -D_BSD_SOURCE
endif

ifdef LLAMA_DEBUG
	MK_CFLAGS   += -O0 -g
	MK_CXXFLAGS += -O0 -g
	MK_LDFLAGS  += -g

	ifeq ($(UNAME_S),Linux)
		MK_CXXFLAGS += -Wp,-D_GLIBCXX_ASSERTIONS
	endif
else
	MK_CPPFLAGS += -DNDEBUG
endif

ifdef LLAMA_SANITIZE_THREAD
	MK_CFLAGS   += -fsanitize=thread -g
	MK_CXXFLAGS += -fsanitize=thread -g
	MK_LDFLAGS  += -fsanitize=thread -g
endif

ifdef LLAMA_SANITIZE_ADDRESS
	MK_CFLAGS   += -fsanitize=address -fno-omit-frame-pointer -g
	MK_CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
	MK_LDFLAGS  += -fsanitize=address -fno-omit-frame-pointer -g
endif

ifdef LLAMA_SANITIZE_UNDEFINED
	MK_CFLAGS   += -fsanitize=undefined -g
	MK_CXXFLAGS += -fsanitize=undefined -g
	MK_LDFLAGS  += -fsanitize=undefined -g
endif

ifdef LLAMA_SERVER_VERBOSE
	MK_CPPFLAGS += -DSERVER_VERBOSE=$(LLAMA_SERVER_VERBOSE)
endif


ifdef LLAMA_CODE_COVERAGE
	MK_CXXFLAGS += -fprofile-arcs -ftest-coverage -dumpbase ''
endif

ifdef LLAMA_DISABLE_LOGS
	MK_CPPFLAGS += -DLOG_DISABLE_LOGS
endif # LLAMA_DISABLE_LOGS

# warnings
WARN_FLAGS    = -Wall -Wextra -Wpedantic -Wcast-qual -Wno-unused-function
MK_CFLAGS    += $(WARN_FLAGS) -Wshadow -Wstrict-prototypes -Wpointer-arith -Wmissing-prototypes -Werror=implicit-int \
				-Werror=implicit-function-declaration
MK_CXXFLAGS  += $(WARN_FLAGS) -Wmissing-noreturn

# this version of Apple ld64 is buggy
ifneq '' '$(findstring dyld-1015.7,$(shell $(CC) $(LDFLAGS) -Wl,-v 2>&1))'
	MK_CPPFLAGS += -DHAVE_BUGGY_APPLE_LINKER
endif

# OS specific
# TODO: support Windows
ifneq '' '$(filter $(UNAME_S),Linux Darwin FreeBSD NetBSD OpenBSD Haiku)'
	MK_CFLAGS   += -pthread
	MK_CXXFLAGS += -pthread
endif

# detect Windows
ifneq ($(findstring _NT,$(UNAME_S)),)
	_WIN32 := 1
endif

# library name prefix
ifneq ($(_WIN32),1)
	LIB_PRE := lib
endif

# Dynamic Shared Object extension
ifneq ($(_WIN32),1)
	DSO_EXT := .so
else
	DSO_EXT := .dll
endif

# Windows Sockets 2 (Winsock) for network-capable apps
ifeq ($(_WIN32),1)
	LWINSOCK2 := -lws2_32
endif

ifdef LLAMA_GPROF
	MK_CFLAGS   += -pg
	MK_CXXFLAGS += -pg
endif
ifdef LLAMA_PERF
	MK_CPPFLAGS += -DGGML_PERF
endif

# Architecture specific
# TODO: probably these flags need to be tweaked on some architectures
#       feel free to update the Makefile for your architecture and send a pull request or issue

ifndef RISCV

ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 i686 amd64))
	# Use all CPU extensions that are available:
	MK_CFLAGS     += -march=native -mtune=native
	HOST_CXXFLAGS += -march=native -mtune=native

	# Usage AVX-only
	#MK_CFLAGS   += -mfma -mf16c -mavx
	#MK_CXXFLAGS += -mfma -mf16c -mavx

	# Usage SSSE3-only (Not is SSE3!)
	#MK_CFLAGS   += -mssse3
	#MK_CXXFLAGS += -mssse3
endif

ifneq '' '$(findstring mingw,$(shell $(CC) -dumpmachine))'
	# The stack is only 16-byte aligned on Windows, so don't let gcc emit aligned moves.
	# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412
	# https://github.com/ggerganov/llama.cpp/issues/2922
	MK_CFLAGS   += -Xassembler -muse-unaligned-vector-move
	MK_CXXFLAGS += -Xassembler -muse-unaligned-vector-move

	# Target Windows 8 for PrefetchVirtualMemory
	MK_CPPFLAGS += -D_WIN32_WINNT=0x602
endif

ifneq ($(filter aarch64%,$(UNAME_M)),)
	# Apple M1, M2, etc.
	# Raspberry Pi 3, 4, Zero 2 (64-bit)
	# Nvidia Jetson
	MK_CFLAGS   += -mcpu=native
	MK_CXXFLAGS += -mcpu=native
	JETSON_RELEASE_INFO = $(shell jetson_release)
	ifdef JETSON_RELEASE_INFO
		ifneq ($(filter TX2%,$(JETSON_RELEASE_INFO)),)
			JETSON_EOL_MODULE_DETECT = 1
			CC = aarch64-unknown-linux-gnu-gcc
			cxx = aarch64-unknown-linux-gnu-g++
		endif
	endif
endif

ifneq ($(filter armv6%,$(UNAME_M)),)
	# Raspberry Pi 1, Zero
	MK_CFLAGS   += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access
	MK_CXXFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access
endif

ifneq ($(filter armv7%,$(UNAME_M)),)
	# Raspberry Pi 2
	MK_CFLAGS   += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access -funsafe-math-optimizations
	MK_CXXFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access -funsafe-math-optimizations
endif

ifneq ($(filter armv8%,$(UNAME_M)),)
	# Raspberry Pi 3, 4, Zero 2 (32-bit)
	MK_CFLAGS   += -mfp16-format=ieee -mno-unaligned-access
	MK_CXXFLAGS += -mfp16-format=ieee -mno-unaligned-access
endif

ifneq ($(filter ppc64%,$(UNAME_M)),)
	POWER9_M := $(shell grep "POWER9" /proc/cpuinfo)
	ifneq (,$(findstring POWER9,$(POWER9_M)))
		MK_CFLAGS   += -mcpu=power9
		MK_CXXFLAGS += -mcpu=power9
	endif
endif

ifneq ($(filter ppc64le%,$(UNAME_M)),)
	MK_CFLAGS   += -mcpu=powerpc64le
	MK_CXXFLAGS += -mcpu=powerpc64le
	CUDA_POWER_ARCH = 1
endif

else
	MK_CFLAGS   += -march=rv64gcv -mabi=lp64d
	MK_CXXFLAGS += -march=rv64gcv -mabi=lp64d
endif

ifdef LLAMA_QKK_64
	MK_CPPFLAGS += -DGGML_QKK_64
endif

ifndef LLAMA_NO_ACCELERATE
	# Mac OS - include Accelerate framework.
	# `-framework Accelerate` works both with Apple Silicon and Mac Intel
	ifeq ($(UNAME_S),Darwin)
		MK_CPPFLAGS += -DGGML_USE_ACCELERATE
		MK_CPPFLAGS += -DACCELERATE_NEW_LAPACK
		MK_CPPFLAGS += -DACCELERATE_LAPACK_ILP64
		MK_LDFLAGS  += -framework Accelerate
	endif
endif # LLAMA_NO_ACCELERATE

ifdef LLAMA_MPI
	MK_CPPFLAGS += -DGGML_USE_MPI
	MK_CFLAGS   += -Wno-cast-qual
	MK_CXXFLAGS += -Wno-cast-qual
	OBJS        += ggml-mpi.o
endif # LLAMA_MPI

ifdef LLAMA_OPENBLAS
	MK_CPPFLAGS += -DGGML_USE_OPENBLAS $(shell pkg-config --cflags-only-I openblas)
	MK_CFLAGS   += $(shell pkg-config --cflags-only-other openblas)
	MK_LDFLAGS  += $(shell pkg-config --libs openblas)
endif # LLAMA_OPENBLAS

ifdef LLAMA_BLIS
	MK_CPPFLAGS += -DGGML_USE_OPENBLAS -I/usr/local/include/blis -I/usr/include/blis
	MK_LDFLAGS  += -lblis -L/usr/local/lib
endif # LLAMA_BLIS

ifdef LLAMA_CUBLAS
	MK_CPPFLAGS  += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/x86_64-linux/include -I/usr/local/cuda/targets/aarch64-linux/include
	MK_LDFLAGS   += -lcuda -lcublas -lculibos -lcudart -lcublasLt -lpthread -ldl -lrt -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -L$(CUDA_PATH)/targets/x86_64-linux/lib -L/usr/local/cuda/targets/aarch64-linux/lib -L/usr/lib/wsl/lib
	OBJS         += ggml-cuda.o
	MK_NVCCFLAGS  = -use_fast_math
ifndef JETSON_EOL_MODULE_DETECT
	MK_NVCCFLAGS += --forward-unknown-to-host-compiler
endif # JETSON_EOL_MODULE_DETECT
ifdef LLAMA_DEBUG
	MK_NVCCFLAGS += -lineinfo
endif # LLAMA_DEBUG
ifdef LLAMA_CUDA_NVCC
	NVCC = $(LLAMA_CUDA_NVCC)
else
	NVCC = nvcc
endif #LLAMA_CUDA_NVCC
ifdef CUDA_DOCKER_ARCH
	MK_NVCCFLAGS += -Wno-deprecated-gpu-targets -arch=$(CUDA_DOCKER_ARCH)
else ifndef CUDA_POWER_ARCH
	MK_NVCCFLAGS += -arch=all
endif # CUDA_DOCKER_ARCH
ifdef LLAMA_CUDA_FORCE_DMMV
	MK_NVCCFLAGS += -DGGML_CUDA_FORCE_DMMV
endif # LLAMA_CUDA_FORCE_DMMV
ifdef LLAMA_CUDA_FORCE_MMQ
	MK_NVCCFLAGS += -DGGML_CUDA_FORCE_MMQ
endif # LLAMA_CUDA_FORCE_MMQ
ifdef LLAMA_CUDA_DMMV_X
	MK_NVCCFLAGS += -DGGML_CUDA_DMMV_X=$(LLAMA_CUDA_DMMV_X)
else
	MK_NVCCFLAGS += -DGGML_CUDA_DMMV_X=32
endif # LLAMA_CUDA_DMMV_X
ifdef LLAMA_CUDA_MMV_Y
	MK_NVCCFLAGS += -DGGML_CUDA_MMV_Y=$(LLAMA_CUDA_MMV_Y)
else ifdef LLAMA_CUDA_DMMV_Y
	MK_NVCCFLAGS += -DGGML_CUDA_MMV_Y=$(LLAMA_CUDA_DMMV_Y) # for backwards compatibility
else
	MK_NVCCFLAGS += -DGGML_CUDA_MMV_Y=1
endif # LLAMA_CUDA_MMV_Y
ifdef LLAMA_CUDA_F16
	MK_NVCCFLAGS += -DGGML_CUDA_F16
endif # LLAMA_CUDA_F16
ifdef LLAMA_CUDA_DMMV_F16
	MK_NVCCFLAGS += -DGGML_CUDA_F16
endif # LLAMA_CUDA_DMMV_F16
ifdef LLAMA_CUDA_KQUANTS_ITER
	MK_NVCCFLAGS += -DK_QUANTS_PER_ITERATION=$(LLAMA_CUDA_KQUANTS_ITER)
else
	MK_NVCCFLAGS += -DK_QUANTS_PER_ITERATION=2
endif
ifdef LLAMA_CUDA_PEER_MAX_BATCH_SIZE
	MK_NVCCFLAGS += -DGGML_CUDA_PEER_MAX_BATCH_SIZE=$(LLAMA_CUDA_PEER_MAX_BATCH_SIZE)
else
	MK_NVCCFLAGS += -DGGML_CUDA_PEER_MAX_BATCH_SIZE=128
endif # LLAMA_CUDA_PEER_MAX_BATCH_SIZE
#ifdef LLAMA_CUDA_CUBLAS
#	MK_NVCCFLAGS += -DGGML_CUDA_CUBLAS
#endif # LLAMA_CUDA_CUBLAS
ifdef LLAMA_CUDA_CCBIN
	MK_NVCCFLAGS += -ccbin $(LLAMA_CUDA_CCBIN)
endif
ggml-cuda.o: ggml-cuda.cu ggml-cuda.h
ifdef JETSON_EOL_MODULE_DETECT
	$(NVCC) -I. -Icommon -D_XOPEN_SOURCE=600 -D_GNU_SOURCE -DNDEBUG -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I/usr/local/cuda/targets/aarch64-linux/include -std=c++11 -O3 $(NVCCFLAGS) -Xcompiler "$(CUDA_CXXFLAGS)" -c $< -o $@
else
	$(NVCC) $(BASE_CXXFLAGS) $(NVCCFLAGS) -Wno-pedantic -Xcompiler "$(CUDA_CXXFLAGS)" -c $< -o $@
endif # JETSON_EOL_MODULE_DETECT
endif # LLAMA_CUBLAS

ifdef LLAMA_CLBLAST

	MK_CPPFLAGS += -DGGML_USE_CLBLAST $(shell pkg-config --cflags-only-I clblast OpenCL)
	MK_CFLAGS   += $(shell pkg-config --cflags-only-other clblast OpenCL)
	MK_CXXFLAGS += $(shell pkg-config --cflags-only-other clblast OpenCL)

	# Mac provides OpenCL as a framework
	ifeq ($(UNAME_S),Darwin)
		MK_LDFLAGS += -lclblast -framework OpenCL
	else
		MK_LDFLAGS += $(shell pkg-config --libs clblast OpenCL)
	endif
	OBJS    += ggml-opencl.o

ggml-opencl.o: ggml-opencl.cpp ggml-opencl.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
endif # LLAMA_CLBLAST

ifdef LLAMA_VULKAN
	MK_CPPFLAGS  += -DGGML_USE_VULKAN
	MK_LDFLAGS += -lvulkan
	OBJS    += ggml-vulkan.o

ifdef LLAMA_VULKAN_CHECK_RESULTS
	MK_CPPFLAGS  += -DGGML_VULKAN_CHECK_RESULTS
endif

ggml-vulkan.o: ggml-vulkan.cpp ggml-vulkan.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
endif # LLAMA_VULKAN

ifdef LLAMA_HIPBLAS

	ifeq ($(wildcard /opt/rocm),)
		ROCM_PATH	?= /usr
		GPU_TARGETS ?= $(shell $(shell which amdgpu-arch))
	else
		ROCM_PATH	?= /opt/rocm
		GPU_TARGETS ?= $(shell $(ROCM_PATH)/llvm/bin/amdgpu-arch)
	endif
	HIPCC                   ?= $(ROCM_PATH)/bin/hipcc
	LLAMA_CUDA_DMMV_X       ?= 32
	LLAMA_CUDA_MMV_Y        ?= 1
	LLAMA_CUDA_KQUANTS_ITER ?= 2
	MK_CPPFLAGS += -DGGML_USE_HIPBLAS -DGGML_USE_CUBLAS
ifdef LLAMA_HIP_UMA
	MK_CPPFLAGS += -DGGML_HIP_UMA
endif # LLAMA_HIP_UMA
	MK_LDFLAGS  += -L$(ROCM_PATH)/lib -Wl,-rpath=$(ROCM_PATH)/lib
	MK_LDFLAGS	+= -lhipblas -lamdhip64 -lrocblas
	HIPFLAGS    += $(addprefix --offload-arch=,$(GPU_TARGETS))
	HIPFLAGS    += -DGGML_CUDA_DMMV_X=$(LLAMA_CUDA_DMMV_X)
	HIPFLAGS    += -DGGML_CUDA_MMV_Y=$(LLAMA_CUDA_MMV_Y)
	HIPFLAGS    += -DK_QUANTS_PER_ITERATION=$(LLAMA_CUDA_KQUANTS_ITER)
ifdef LLAMA_CUDA_FORCE_DMMV
	HIPFLAGS 	+= -DGGML_CUDA_FORCE_DMMV
endif # LLAMA_CUDA_FORCE_DMMV
	OBJS        += ggml-cuda.o
ggml-cuda.o: ggml-cuda.cu ggml-cuda.h
	$(HIPCC) $(CXXFLAGS) $(HIPFLAGS) -x hip -c -o $@ $<
endif # LLAMA_HIPBLAS

ifdef LLAMA_METAL
	MK_CPPFLAGS += -DGGML_USE_METAL
	MK_LDFLAGS  += -framework Foundation -framework Metal -framework MetalKit
	OBJS		+= ggml-metal.o
ifdef LLAMA_METAL_NDEBUG
	MK_CPPFLAGS += -DGGML_METAL_NDEBUG
endif
endif # LLAMA_METAL

ifdef LLAMA_METAL
ggml-metal.o: ggml-metal.m ggml-metal.h
	$(CC) $(CFLAGS) -c $< -o $@
endif # LLAMA_METAL

ifdef LLAMA_MPI
ggml-mpi.o: ggml-mpi.c ggml-mpi.h
	$(CC) $(CFLAGS) -c $< -o $@
endif # LLAMA_MPI

GF_CC := $(CC)

# combine build flags with cmdline overrides
override CFLAGS    := $(MK_CPPFLAGS) $(CPPFLAGS) $(MK_CFLAGS) $(GF_CFLAGS) $(CFLAGS)
BASE_CXXFLAGS      := $(MK_CPPFLAGS) $(CPPFLAGS) $(MK_CXXFLAGS) $(CXXFLAGS)
override CXXFLAGS  := $(BASE_CXXFLAGS) $(HOST_CXXFLAGS) $(GF_CXXFLAGS)
override NVCCFLAGS := $(MK_NVCCFLAGS) $(NVCCFLAGS)
override LDFLAGS   := $(MK_LDFLAGS) $(LDFLAGS)

# identify CUDA host compiler
ifdef LLAMA_CUBLAS
GF_CC := $(NVCC) $(NVCCFLAGS) 2>/dev/null .c -Xcompiler
CUDA_CXXFLAGS := $(GF_CXXFLAGS)
endif

#
# Print build information
#

$(info I llama.cpp build info: )
$(info I UNAME_S:   $(UNAME_S))
$(info I UNAME_P:   $(UNAME_P))
$(info I UNAME_M:   $(UNAME_M))
$(info I CFLAGS:    $(CFLAGS))
$(info I CXXFLAGS:  $(CXXFLAGS))
$(info I NVCCFLAGS: $(NVCCFLAGS))
$(info I LDFLAGS:   $(LDFLAGS))
$(info I CC:        $(shell $(CC) --version | head -n 1))
$(info I CXX:       $(shell $(CXX) --version | head -n 1))
$(info )

#
# Build stuff
#

ggml.o: ggml.c ggml.h ggml-cuda.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-alloc.o: ggml-alloc.c ggml.h ggml-alloc.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-backend.o: ggml-backend.c ggml.h ggml-backend.h
	$(CC)  $(CFLAGS)   -c $< -o $@

ggml-quants.o: ggml-quants.c ggml.h ggml-quants.h
	$(CC) $(CFLAGS)    -c $< -o $@

OBJS += ggml-alloc.o ggml-backend.o ggml-quants.o

llama.o: llama.cpp ggml.h ggml-alloc.h ggml-backend.h ggml-cuda.h llama.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

COMMON_H_DEPS = common.h sampling.h dtypes.h
COMMON_DEPS   = common.o sampling.o grammar-parser.o

common.o: common.cpp $(COMMON_H_DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

sampling.o: sampling.cpp $(COMMON_H_DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

grammar-parser.o: grammar-parser.cpp grammar-parser.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clip.o: clip.cpp clip.h stb_image.h
	$(CXX) $(CXXFLAGS) -Wno-cast-qual -c $< -o $@

brain.o: brain.cpp brain.h vecstore.h
	$(CXX) $(CXXFLAGS) -Wno-cast-qual -c $< -o $@

netclient.o: netclient.cpp netclient.h brain.h server/httplib.h server/base64m.h server/codec.h
	$(CXX) $(CXXFLAGS) -std=c++2a -Iserver -c $< -o $@

anna: anna.cpp ggml.o llama.o common.o sampling.o clip.o brain.o netclient.o grammar-parser.o $(OBJS) $(COMMON_H_DEPS)
	$(CXX) $(CXXFLAGS) -std=c++2a $(filter-out %.h,$^) -o $@ $(LDFLAGS)

libanna.a: ggml.o llama.o common.o sampling.o clip.o brain.o netclient.o grammar-parser.o $(OBJS) $(COMMON_H_DEPS)
	ar cru $@ $^

anna_server: server/server.cpp server/base64m.h server/httplib.h server/codec.h libanna.a
	$(CXX) $(CXXFLAGS) -std=c++2a $(filter-out %.h,$^) -o $@ $(LDFLAGS)

clean:
	rm -vrf *.o tests/*.o *.so *.a *.dll *.dot $(COV_TARGETS) $(BUILD_TARGETS) $(TEST_TARGETS)
