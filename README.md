# SPECInt2006-micro

SPECInt2006의 특성을 반영한 마이크로 벤치마크 스위트입니다.

## 특징
https://github.com/SweetWeeds/specint-2006-micro
- **플랫폼 지원**:
  - x86-64 Linux (native)
  - nexus-am (riscv64-xs, riscv64-xs-flash)
- **베어메탈 지원**: RISC-V에서 OS 없이 실행 가능
- **XiangShan 호환**: XiangShan RTL 시뮬레이터, NEMU와 함께 사용

## 마이크로 커널

SPECInt2006 벤치마크별로 그룹화된 15개의 커널:

### 400.perlbench
| 커널 | 패턴 |
|------|------|
| `hash_lookup` | 해시 테이블 조회 (Perl HV) |
| `string_match` | 문자열 패턴 매칭 (KMP/BMH) |

### 401.bzip2
| 커널 | 패턴 |
|------|------|
| `bwt_sort` | Burrows-Wheeler 변환 |
| `huffman_tree` | 허프만 트리 생성 |

### 403.gcc
| 커널 | 패턴 |
|------|------|
| `tree_walk` | AST 트리 순회 및 상수 접기 |

### 429.mcf
| 커널 | 패턴 |
|------|------|
| `graph_simplex` | 네트워크 심플렉스 알고리즘 |

### 445.gobmk
| 커널 | 패턴 |
|------|------|
| `go_liberty` | Go 보드 자유도 계산/그래프 순회 |

### 456.hmmer
| 커널 | 패턴 |
|------|------|
| `viterbi_hmm` | Viterbi 동적 프로그래밍 |

### 458.sjeng
| 커널 | 패턴 |
|------|------|
| `game_tree` | 알파-베타 탐색 |

### 462.libquantum
| 커널 | 패턴 |
|------|------|
| `quantum_sim` | 양자 게이트 시뮬레이션 |

### 464.h264ref
| 커널 | 패턴 |
|------|------|
| `dct_4x4` | H.264 4x4 DCT 변환 |
| `block_sad` | 모션 추정 SAD (전역/다이아몬드 탐색) |

### 471.omnetpp
| 커널 | 패턴 |
|------|------|
| `priority_queue` | 우선순위 큐 (이벤트 시뮬레이션) |

### 473.astar
| 커널 | 패턴 |
|------|------|
| `astar_path` | A* 경로 탐색 |

### 483.xalancbmk
| 커널 | 패턴 |
|------|------|
| `xpath_eval` | XPath 트리 순회/쿼리 평가 |

## 빌드
각 항목별로 누락된 커널이 있는지 원본 코드 (/home/han/workspace/spec-int-2006/benchspec/CPU2006)를 기반으로 점검하고 누락된 커널을 구현할 계획을 세워라.

### 요구사항

- GCC (x86-64 native 빌드)
- nexus-am 환경 + RISC-V GCC 툴체인 (베어메탈 빌드)

### 빌드 명령

```bash
# x86-64 Linux 네이티브 빌드
make ARCH=native
./build/native/specint2006-micro

# nexus-am 베어메탈 빌드 (환경 설정 필요)
source env.sh
cd nexus-am/apps/specint2006-micro
make ARCH=riscv64-xs

# Flash 기반 빌드
make ARCH=riscv64-xs-flash

# 정리
make ARCH=native clean   # native 빌드 정리
make clean               # nexus-am 빌드 정리
```

## 실행

### NEMU에서 실행

```bash
# NEMU 빌드 (처음만)
cd $NEMU_HOME
make riscv64-xs_defconfig
make -j

# 벤치마크 실행
./build/riscv64-nemu-interpreter -b $AM_HOME/apps/specint2006-micro/build/specint2006-micro-riscv64-xs.bin
```

### XiangShan EMU에서 실행

```bash
# XiangShan EMU 빌드 (처음만)
cd $NOOP_HOME
make emu

# 벤치마크 실행
./build/emu -i $AM_HOME/apps/specint2006-micro/build/specint2006-micro-riscv64-xs.bin \
            --diff $NEMU_HOME/build/riscv64-nemu-interpreter-so
```

## 출력 예시

```
================================================================================
SPECInt2006-micro Benchmark Results
Architecture: x86-64
Platform: linux
================================================================================

Kernel                 Min Cycles   Avg Cycles   Max Cycles   Checksum Status
--------------------------------------------------------------------------------

[400.perlbench]
hash_lookup                  1829         2003         2460 0x66cc7e33 PASS
string_match                52800        53712        55920 0xa65ca75f PASS

[401.bzip2]
bwt_sort                     8340         8910        10200 0x59292766 PASS
huffman_tree                 6420         9762        16740 0x358f87d4 PASS

[403.gcc]
tree_walk                    5040         5621         6990 0xe61aff8c PASS

[429.mcf]
graph_simplex                 570          617          720 0x4ab0f7b7 PASS

[445.gobmk]
go_liberty                  12810        14531        17850 0x5f8022b3 PASS

[456.hmmer]
viterbi_hmm                 13500        13530        13620 0x49dd42c1 PASS

[458.sjeng]
game_tree                      29           29           30 0xf1344a44 PASS

[462.libquantum]
quantum_sim                 14970        15318        16140 0x3789d7b5 PASS

[464.h264ref]
dct_4x4                      1770         1776         1800 0x6ab70fcc PASS
block_sad                   64079        64433        65160 0x876e8356 PASS

[471.omnetpp]
priority_queue              13349        13859        15360 0xd8e19160 PASS

[473.astar]
astar_path                 466890       505740       554790 0x06c3963a PASS

[483.xalancbmk]
xpath_eval                   3090         3143         3240 0xf53ddd8b PASS
--------------------------------------------------------------------------------

Summary:
  Kernels:        15 total, 15 passed, 0 failed
  Total Cycles:   712984
  Geomean Cycles: 7213
  Score/GHz:      138638 (higher is better)
```

## 설정 조정

`Makefile`에서 커널 파라미터를 조정하여 목표 사이클 수를 맞출 수 있습니다:

```makefile
# 해시 테이블 크기
CFLAGS += -DHASH_NUM_BUCKETS=256
CFLAGS += -DHASH_NUM_ENTRIES=512
CFLAGS += -DHASH_NUM_LOOKUPS=100

# BWT 블록 크기
CFLAGS += -DBWT_BLOCK_SIZE=512

# Viterbi 시퀀스 길이
CFLAGS += -DHMM_SEQ_LENGTH=50
CFLAGS += -DHMM_MODEL_SIZE=32

# Go liberty (445.gobmk)
CFLAGS += -DGO_BOARD_SIZE=9
CFLAGS += -DGO_NUM_STONES=40
CFLAGS += -DGO_NUM_QUERIES=50

# Quantum simulation (462.libquantum)
CFLAGS += -DQUANTUM_NUM_QUBITS=6
CFLAGS += -DQUANTUM_NUM_GATES=20

# A* pathfinding (473.astar)
CFLAGS += -DASTAR_MAP_SIZE=32
CFLAGS += -DASTAR_NUM_OBSTACLES=200
CFLAGS += -DASTAR_NUM_QUERIES=10

# XPath evaluation (483.xalancbmk)
CFLAGS += -DXPATH_NUM_NODES=256
CFLAGS += -DXPATH_NUM_QUERIES=20
```

## 디렉토리 구조

```
specint2006-micro/
├── src/              # 소스 코드
│   ├── bench.h       # 벤치마크 API 헤더
│   ├── main.c        # 메인 진입점
│   └── *.c           # 각 커널 구현
├── build/            # 빌드 출력
├── Makefile          # nexus-am 빌드 설정
└── README.md
```

## 라이선스

BSD-3-Clause
