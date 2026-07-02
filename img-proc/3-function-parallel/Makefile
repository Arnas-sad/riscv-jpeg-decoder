USER_LIBS := $(wildcard lib*)
DIRS := $(wildcard partition_* arm_*)
# TODO: fix this hack: make partitions first to generate vep_memory.* files  - shoudl have more dependencies for ARM

# all standard libraries are in WORKSPACE/../vep_0
# dependencies thereon are dealt with by the share/make_*.mk
# make all libraries and partitions
all: $(foreach lib,$(USER_LIBS),make-$(lib)) $(foreach partition,$(DIRS),make-$(partition))

# not ideal since calls make for each dir & partition, but at least properly deal with dependencies
# also, all partitions (re)compile shared_memories/*.c, which is not ideal  
make-%: %
	make -C $^

clean veryclean: $(foreach dir, $(DIRS), clean-$(dir)) $(foreach lib,$(USER_LIBS),clean-$(lib))

clean-%: %
	make -C $^ clean

.PHONY: all clean veryclean
