include ../../vep_0/share/make_common.mk
# JPEG sequential decoder: no FIFO library; -DJPEG_DRAM_INPUT switches FGETC
# in 5lib0.c to the windowed DRAM reader (ARM build keeps direct-array access).
USER_LIBS :=
USER_CFLAGS := -DJPEG_DRAM_INPUT
USER_LINK_FLAGS :=
include ../../vep_0/share/make_tile.mk
