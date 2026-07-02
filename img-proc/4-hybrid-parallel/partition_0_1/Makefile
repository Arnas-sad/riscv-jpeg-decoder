include ../../vep_0/share/make_common.mk
# 2d hybrid: VLD+IQZZ source actor. libfifo is a system library (auto-
# linked, see make_tile.mk), so no USER_LIBS needed. -DJPEG_DRAM_INPUT selects
# the windowed DRAM reader in 5lib0.c/dram_input.c (this is the only DMA stage).
USER_LIBS :=
USER_CFLAGS := -DJPEG_DRAM_INPUT
USER_LINK_FLAGS :=
include ../../vep_0/share/make_tile.mk
