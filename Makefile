#CC=clang-14
#CXX=clang++-14
#LLAMA_CUBLAS=1
#LLAMA_DEBUG=1

all: anna libanna.a
.PHONY: all

# Define the default target now so that it is always the first target
BUILD_TARGETS = anna

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
else
	MK_CPPFLAGS += -DNDEBUG
	# -Ofast tends to produce faster code, but may not be available for some compilers.
	MK_CFLAGS        += -Ofast
	MK_HOST_CXXFLAGS += -Ofast
	MK_CUDA_CXXFLAGS += -O3
endif

ifdef LLAMA_SERVER_VERBOSE
	MK_CPPFLAGS += -DSERVER_VERBOSE=$(LLAMA_SERVER_VERBOSE)
endif

ifdef LLAMA_DISABLE_LOGS
	MK_CPPFLAGS += -DLOG_DISABLE_LOGS
endif # LLAMA_DISABLE_LOGS

# warnings
MK_CFLAGS    += -Wall -Wextra -Wpedantic -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith \
				-Wmissing-prototypes -Werror=implicit-int -Wno-unused-function \
				-Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter
MK_CXXFLAGS  += -Wall -Wextra -Wpedantic -Wcast-qual -Wmissing-declarations -Wno-unused-function -Wno-multichar \
				-Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-parameter

ifdef LLAMA_CUBLAS
	MK_CPPFLAGS  += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/x86_64-linux/include
	MK_LDFLAGS   += -lcublas -lculibos -lcudart -lcublasLt -lpthread -ldl -lrt -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -L$(CUDA_PATH)/targets/x86_64-linux/lib
	OBJS      += ggml-cuda.o
	NVCCFLAGS = --forward-unknown-to-host-compiler -use_fast_math
ifdef LLAMA_CUDA_NVCC
	NVCC = $(LLAMA_CUDA_NVCC)
else
	NVCC = nvcc
endif #LLAMA_CUDA_NVCC
ifdef CUDA_DOCKER_ARCH
	NVCCFLAGS += -Wno-deprecated-gpu-targets -arch=$(CUDA_DOCKER_ARCH)
else
	NVCCFLAGS += -arch=all
endif # CUDA_DOCKER_ARCH
ifdef LLAMA_CUDA_FORCE_DMMV
	NVCCFLAGS += -DGGML_CUDA_FORCE_DMMV
endif # LLAMA_CUDA_FORCE_DMMV
ifdef LLAMA_CUDA_DMMV_X
	NVCCFLAGS += -DGGML_CUDA_DMMV_X=$(LLAMA_CUDA_DMMV_X)
else
	NVCCFLAGS += -DGGML_CUDA_DMMV_X=32
endif # LLAMA_CUDA_DMMV_X
ifdef LLAMA_CUDA_MMV_Y
	NVCCFLAGS += -DGGML_CUDA_MMV_Y=$(LLAMA_CUDA_MMV_Y)
else ifdef LLAMA_CUDA_DMMV_Y
	NVCCFLAGS += -DGGML_CUDA_MMV_Y=$(LLAMA_CUDA_DMMV_Y) # for backwards compatibility
else
	NVCCFLAGS += -DGGML_CUDA_MMV_Y=1
endif # LLAMA_CUDA_MMV_Y
ifdef LLAMA_CUDA_F16
	NVCCFLAGS += -DGGML_CUDA_F16
endif # LLAMA_CUDA_F16
ifdef LLAMA_CUDA_DMMV_F16
	NVCCFLAGS += -DGGML_CUDA_F16
endif # LLAMA_CUDA_DMMV_F16
ifdef LLAMA_CUDA_KQUANTS_ITER
	NVCCFLAGS += -DK_QUANTS_PER_ITERATION=$(LLAMA_CUDA_KQUANTS_ITER)
else
	NVCCFLAGS += -DK_QUANTS_PER_ITERATION=2
endif
ifdef LLAMA_CUDA_PEER_MAX_BATCH_SIZE
	NVCCFLAGS += -DGGML_CUDA_PEER_MAX_BATCH_SIZE=$(LLAMA_CUDA_PEER_MAX_BATCH_SIZE)
else
	NVCCFLAGS += -DGGML_CUDA_PEER_MAX_BATCH_SIZE=128
endif # LLAMA_CUDA_PEER_MAX_BATCH_SIZE
#ifdef LLAMA_CUDA_CUBLAS
#	NVCCFLAGS += -DGGML_CUDA_CUBLAS
#endif # LLAMA_CUDA_CUBLAS
ifdef LLAMA_CUDA_CCBIN
	NVCCFLAGS += -ccbin $(LLAMA_CUDA_CCBIN)
endif
ggml-cuda.o: ggml-cuda.cu ggml-cuda.h
	$(NVCC) $(NVCCFLAGS) -Wno-pedantic -c $< -o $@
endif # LLAMA_CUBLAS

# TODO(cebtenzzre): remove this once PR #2632 gets merged
TTFS_CXXFLAGS = $(CXXFLAGS) -Wno-missing-declarations

ifneq '' '$(findstring clang,$(shell $(CXX) --version))'
	# clang++ only
	MK_CXXFLAGS   += -Wmissing-prototypes
	TTFS_CXXFLAGS += -Wno-missing-prototypes
else
	# g++ only
	MK_CXXFLAGS += -Wno-format-truncation -Wno-array-bounds
endif


# Architecture specific
# TODO: probably these flags need to be tweaked on some architectures
#       feel free to update the Makefile for your architecture and send a pull request or issue

ifndef RISCV

ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 i686 amd64))
	# Use all CPU extensions that are available:
	MK_CFLAGS   += -march=native -mtune=native
	MK_HOST_CXXFLAGS += -march=native -mtune=native

	# Usage AVX-only
	#MK_CFLAGS   += -mfma -mf16c -mavx
	#MK_CXXFLAGS += -mfma -mf16c -mavx

	# Usage SSSE3-only (Not is SSE3!)
	#MK_CFLAGS   += -mssse3
	#MK_CXXFLAGS += -mssse3
endif

# The stack is only 16-byte aligned on Windows, so don't let gcc emit aligned moves.
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412
# https://github.com/ggerganov/llama.cpp/issues/2922
ifneq '' '$(findstring mingw,$(shell $(CC) -dumpmachine))'
	MK_CFLAGS   += -Xassembler -muse-unaligned-vector-move
	MK_CXXFLAGS += -Xassembler -muse-unaligned-vector-move
endif

ifneq ($(filter aarch64%,$(UNAME_M)),)
	# Apple M1, M2, etc.
	# Raspberry Pi 3, 4, Zero 2 (64-bit)
	MK_CFLAGS   += -mcpu=native
	MK_CXXFLAGS += -mcpu=native
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

else
	MK_CFLAGS   += -march=rv64gcv -mabi=lp64d
	MK_CXXFLAGS += -march=rv64gcv -mabi=lp64d
endif

ifndef LLAMA_NO_K_QUANTS
	MK_CPPFLAGS += -DGGML_USE_K_QUANTS
	OBJS     += k_quants.o
ifdef LLAMA_QKK_64
	MK_CPPFLAGS += -DGGML_QKK_64
endif
endif

ifndef LLAMA_NO_K_QUANTS
k_quants.o: k_quants.c k_quants.h
	$(CC) $(CFLAGS) -c $< -o $@
endif # LLAMA_NO_K_QUANTS

# combine build flags with cmdline overrides
override CFLAGS        := $(MK_CPPFLAGS) $(CPPFLAGS) $(MK_CFLAGS) $(CFLAGS)
override CXXFLAGS      := $(MK_CPPFLAGS) $(CPPFLAGS) $(MK_CXXFLAGS) $(CXXFLAGS)
override CUDA_CXXFLAGS := $(MK_CUDA_CXXFLAGS) $(CUDA_CXXFLAGS)
override HOST_CXXFLAGS := $(MK_HOST_CXXFLAGS) $(HOST_CXXFLAGS)
override LDFLAGS       := $(MK_LDFLAGS) $(LDFLAGS)

# save CXXFLAGS before we add host-only options
NVCCFLAGS := $(NVCCFLAGS) $(CXXFLAGS) $(CUDA_CXXFLAGS) -Wno-pedantic -Xcompiler "$(HOST_CXXFLAGS)"
override CXXFLAGS += $(HOST_CXXFLAGS)

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
$(info I CC:        $(CCV))
$(info I CXX:       $(CXXV))
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

OBJS += ggml-alloc.o ggml-backend.o

llama.o: llama.cpp ggml.h ggml-alloc.h ggml-cuda.h llama.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

common.o: common.cpp common.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

sampling.o: sampling.cpp sampling.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clip.o: clip.cpp clip.h stb_image.h
	$(CXX) $(CXXFLAGS) -Wno-cast-qual -c $< -o $@

brain.o: brain.cpp brain.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -vrf *.o tests/*.o *.so *.dll benchmark-matmult build-info.h *.dot $(COV_TARGETS) $(BUILD_TARGETS) $(TEST_TARGETS)

anna: anna.cpp ggml.o llama.o common.o sampling.o clip.o $(OBJS)
	$(CXX) $(CXXFLAGS) -std=c++20 $(filter-out %.h,$^) -o $@ $(LDFLAGS)

libanna.a: ggml.o llama.o common.o sampling.o clip.o brain.o $(OBJS)
	ar cru $@ $^
