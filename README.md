# RISC-V JPEG Decoder on CompSOC

A baseline-JPEG decoder implemented and parallelised on the **CompSOC** mixed-time-criticality
platform, as part of the 5LIB0 Embedded Systems Laboratory (TU/e). The decoder is built once
sequentially and then mapped onto three parallel architectures — **data-parallel**,
**function-parallel**, and **hybrid** — with the function-parallel version modelled as a dataflow
graph and analysed for a worst-case response time (WCRT).

> University group project (3 members). See [Contributors](#contributors) for my role.

## Platform

The target is a Xilinx Zynq board running CompSOC: **three RISC-V tiles at 40 MHz** plus an ARM core
that uploads the JPEG and reads back the output. The properties that shaped every design decision:

- **Bare-metal, time-division-multiplexed (TDM) tiles** — each tile runs a static slot table; a
  partition that misses its slot waits a whole revolution.
- **DRAM only via DMA** — a RISC-V tile cannot dereference DRAM directly; all traffic is
  1 KiB-aligned blocks through a per-tile DMA queue.
- **Tight local memory** — at most 64 KiB per partition.
- **Shared memory** for inter-tile FIFOs; sequential consistency, so `volatile` accesses are enough
  (no atomics or locks).
- The framebuffer must be composed **on the RISC-V**, with completion signalled by zeroing the
  first word of the DRAM table-of-contents.

## The decoder pipeline

`VLD -> IQZZ -> IDCT -> CC -> raster`, operating on Minimum Coded Units (MCUs). A key property for
parallelisation: **VLD (entropy decoding) is inherently sequential** — variable-length codes and a
DC predictor carried between blocks make random access into the bitstream impossible, so every
parallel design has to work around it.

## Implementations (`img-proc/`)

| Version | Idea |
|---|---|
| `1-sequential` | Full pipeline on one partition. Measurement baseline. |
| `2-data-parallel` | All three tiles run the full decoder, each on a disjoint slice of the image, then a gather pass composes the frame. Fastest overall. |
| `3-function-parallel` | The pipeline stages are split across tiles/partitions and connected by FIFOs. Modelled as a dataflow (SDF) graph with self-edges for actor state and analysed for a conservative WCRT — the real-time version. |
| `4-hybrid-parallel` | A mix of data- and function-parallelism: a pipelined front end with a data-split stage. |

At least two versions use multiple partitions on one RISC-V tile, and the versions use different
FIFO token types and depths.

## `libmyfifo/`

A single-producer / single-consumer circular-buffer FIFO library (C-HEAP-style API:
`claim_space` / `release_token` / `claim_token` / `release_space`, plus lifecycle and status calls).
It is the inter-core communication primitive used by the pipelined decoder versions. Tokens are
always delivered correctly and in order under the platform's sequential-consistency model, with no
locks.

## Repository layout

```
img-proc/
  1-sequential/        # single-partition baseline
  2-data-parallel/     # 3 tiles, full decoder each, gather
  3-function-parallel/ # dataflow pipeline + WCRT (real-time version)
  4-hybrid-parallel/   # data + function parallel
libmyfifo/             # SPSC FIFO library
```

Each `img-proc/<version>/` is a self-contained partition set (`partition_<tile>_<partition>/`,
shared memory declarations, and a `vep-config.txt` describing memory and the TDM schedule).

## Results (high level)

- **Data-parallel is the fastest** version, giving roughly a 2x speedup over sequential across the
  benchmark images — bounded by the replicated, unavoidable VLD (an Amdahl-style limit).
- **Function-parallel** is the analysable one: a conservative analytical WCRT holds for every
  admissible input. "Real-time" here means a provable worst-case bound, not necessarily fast.
- **Hybrid** trades redundant VLD work against inter-core communication volume.

The best mapping is content-dependent: small per-MCU images reward fine-grained pipelining, while
large per-MCU images punish the inter-tile copies.

## Building / running

This targets the CompSOC course platform and toolchain (RISC-V bare-metal + the ARM/Linux host
scripts), so it is not a standalone desktop build. Each version is loaded as a VEP onto the RISC-V
tiles; the ARM side uploads the JPEG into DRAM and reads back the decoded framebuffer as a BMP.

## Contributors

This was a group project (three members). **My contributions:** I focused on the JPEG decoder
across all four versions — implementing and optimising each mapping, tuning the TDM slot and
partition/DMA-region layout, and benchmarking the versions over the image set to choose the final
mappings. I resolved the platform bring-up problems that gated the decoders (conflicting DMA-private
memory regions, the partition offset/alignment rules, and the RISC-V gather pass with the
table-of-contents completion signal). I also contributed in part to `libmyfifo` and to the report.
