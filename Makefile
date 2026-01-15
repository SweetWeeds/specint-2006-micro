NAME = specint2006-micro
SRCS = $(shell find -L ./src/ -name "*.c")

# Include paths
INC_DIR += ./src/

# ============================================================================
# Kernel configuration (tune for target cycle count)
# ============================================================================

# Hash lookup
CFLAGS += -DHASH_NUM_BUCKETS=256
CFLAGS += -DHASH_NUM_ENTRIES=512
CFLAGS += -DHASH_NUM_LOOKUPS=100

# BWT sort
CFLAGS += -DBWT_BLOCK_SIZE=512

# Huffman tree
CFLAGS += -DHUFFMAN_SYMBOLS=256

# Viterbi HMM
CFLAGS += -DHMM_SEQ_LENGTH=50
CFLAGS += -DHMM_MODEL_SIZE=32

# DCT 4x4
CFLAGS += -DDCT_NUM_BLOCKS=16

# Game tree
CFLAGS += -DGAME_SEARCH_DEPTH=4
CFLAGS += -DGAME_BRANCHING=8

# Priority queue
CFLAGS += -DPQ_OPERATIONS=256

# Tree walk
CFLAGS += -DTREE_NUM_NODES=256

# String match
CFLAGS += -DTEXT_SIZE=1024
CFLAGS += -DNUM_PATTERNS=10

# Graph simplex
CFLAGS += -DGRAPH_NUM_NODES=64
CFLAGS += -DGRAPH_NUM_ARCS=256
CFLAGS += -DSIMPLEX_ITERATIONS=50

# Block SAD
CFLAGS += -DFRAME_WIDTH=64
CFLAGS += -DFRAME_HEIGHT=64
CFLAGS += -DBLOCK_SIZE=16
CFLAGS += -DSEARCH_RANGE=8

# Go liberty (445.gobmk)
CFLAGS += -DGO_BOARD_SIZE=9
CFLAGS += -DGO_NUM_STONES=40
CFLAGS += -DGO_NUM_QUERIES=50

# Quantum simulation (462.libquantum)
CFLAGS += -DQUANTUM_NUM_QUBITS=6
CFLAGS += -DQUANTUM_NUM_GATES=20
CFLAGS += -DQUANTUM_FACTOR_N=15

# A* pathfinding (473.astar)
CFLAGS += -DASTAR_MAP_SIZE=32
CFLAGS += -DASTAR_NUM_OBSTACLES=200
CFLAGS += -DASTAR_NUM_QUERIES=10

# XPath evaluation (483.xalancbmk)
CFLAGS += -DXPATH_NUM_NODES=256
CFLAGS += -DXPATH_MAX_CHILDREN=8
CFLAGS += -DXPATH_MAX_DEPTH=8
CFLAGS += -DXPATH_NUM_QUERIES=20

# Regex compile (400.perlbench)
CFLAGS += -DREGEX_MAX_STATES=128
CFLAGS += -DREGEX_MAX_TRANS=256
CFLAGS += -DREGEX_NUM_PATTERNS=20

# MTF transform (401.bzip2)
CFLAGS += -DMTF_BLOCK_SIZE=1024
CFLAGS += -DMTF_NUM_BLOCKS=10

# SSA dataflow (403.gcc)
CFLAGS += -DCFG_MAX_BLOCKS=64
CFLAGS += -DCFG_MAX_VARS=32
CFLAGS += -DCFG_NUM_CFGS=5

# Influence field (445.gobmk)
CFLAGS += -DINFLUENCE_BOARD_SIZE=19
CFLAGS += -DINFLUENCE_DILATION=6
CFLAGS += -DINFLUENCE_EROSION=5
CFLAGS += -DINFLUENCE_NUM_EVALS=10

# Forward-backward HMM (456.hmmer)
CFLAGS += -DFB_SEQ_LENGTH=64
CFLAGS += -DFB_NUM_STATES=16
CFLAGS += -DFB_ALPHABET_SIZE=20
CFLAGS += -DFB_NUM_SEQS=5

# Intra prediction (464.h264ref)
CFLAGS += -DINTRA_BLOCK_SIZE=16
CFLAGS += -DINTRA_NUM_BLOCKS=20

# ============================================================================
# Build target selection
# ============================================================================

# If ARCH is set and not 'native', use nexus-am build system
# Otherwise, build for native x86 Linux
ifneq ($(filter native,$(ARCH)),)
  # Explicit native build
  NATIVE_BUILD = 1
else ifdef ARCH
  # nexus-am build (riscv64-xs, etc.)
  include $(AM_HOME)/Makefile.app
else ifdef AM_HOME
  # Default to nexus-am if AM_HOME is set but ARCH is not
  $(info No ARCH specified. Use 'make ARCH=riscv64-xs' for nexus-am or 'make ARCH=native' for x86 Linux)
  include $(AM_HOME)/Makefile.app
else
  # No AM_HOME, default to native build
  NATIVE_BUILD = 1
endif

# ============================================================================
# Native x86 Linux build
# ============================================================================

ifdef NATIVE_BUILD

CC = gcc
OPT ?= -O2
BUILD_DIR = build/native

NATIVE_CFLAGS = $(OPT) -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
NATIVE_CFLAGS += -I./src
NATIVE_CFLAGS += $(CFLAGS)
NATIVE_CFLAGS += -DNATIVE_BUILD

OBJS = $(patsubst ./src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = $(BUILD_DIR)/$(NAME)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "+ LD -> $@"
	@$(CC) $(NATIVE_CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: ./src/%.c
	@mkdir -p $(dir $@)
	@echo "+ CC $<"
	@$(CC) $(NATIVE_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	@$(TARGET)

endif
