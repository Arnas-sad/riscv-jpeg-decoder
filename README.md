# JPEG Decoder on CompSOC (RISC-V)

This is a university group project for the Embedded Systems Laboratory course (5LIB0). The goal was
to write a JPEG image decoder and run it on the CompSOC platform, which has three RISC-V cores. We
wrote the decoder once in a simple sequential way, and then in three parallel ways to make it faster
or to be able to analyse its timing.

## The platform

CompSOC is not a normal computer. The points that affected how we wrote the code:

- There are three RISC-V cores (tiles) at 40 MHz, plus an ARM core that loads the image and reads
  back the result.
- Each core runs on a fixed time schedule. If a task misses its time slot, it waits for the next
  round.
- The cores cannot read the main memory (DRAM) directly. They copy data in and out in 1 KiB blocks
  using DMA.
- Each part of the program has at most 64 KiB of local memory.
- Cores talk to each other through a shared memory area.

## How JPEG decoding works here

A JPEG image is decoded in five steps: VLD, IQZZ, IDCT, CC, and raster. VLD reads the compressed
data, and the other steps turn it into pixels. The important thing is that VLD has to be done in
order and cannot be split up, which limits how much of the decoder can run in parallel.

## What is in this repo

There are two parts: the FIFO library and the decoder.

### libmyfifo

A small FIFO (queue) library we wrote so that one core can send data to another. One side writes,
the other side reads, and the data always arrives in the right order. The decoder versions that
split work across cores use it to pass data between the steps.

### img-proc

The decoder, written in four versions. Each folder is one version.

- **1-sequential**: the simple version. Everything runs on one core, one step after another. This is
  the baseline the others are compared against.
- **2-data-parallel**: all three cores run the whole decoder, but each one only decodes part of the
  image. This is the fastest version.
- **3-function-parallel**: the five steps are split across the cores and connected with FIFOs, like a
  pipeline. This is the version we analysed for worst-case timing.
- **4-hybrid-parallel**: a mix of the two ideas above.

Inside each version folder:

- **partition_X_Y**: the code that runs on core X, part Y.
- **shared_memories**: the description of the shared memory used to pass data between cores.
- **vep-config.txt**: the memory sizes and the time schedule for that version.

## Results, short version

- Data-parallel is the fastest, about two times faster than the sequential version.
- Function-parallel is the version where we could calculate a guaranteed worst-case time.
- Hybrid sits in between.

Which version is best also depends on the image.

## Note

This was a university team project (three people). I did most of the decoder implementation.
