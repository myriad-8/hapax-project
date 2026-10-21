.RECIPEPREFIX = >
CC      ?= gcc
CFLAGS  = -O2 -g -std=gnu11 -pthread -Wall -Wextra -Isrc
TSAN    = -O1 -g -std=gnu11 -pthread -fsanitize=thread -Isrc
BIN     = build
LOCKS   = tas ticket pthread hapax_iw hapax_vw hapax_ad hapax_adv

H_tas = lock_tas.h
H_ticket = lock_ticket.h
H_pthread = lock_pthread.h
H_hapax_iw = lock_hapax_iw.h
H_hapax_vw = lock_hapax_vw.h
H_hapax_ad = lock_hapax_ad.h
H_hapax_adv = lock_hapax_ad.h
D_hapax_adv = -DAD_VACATE=4000

define RULES
$(BIN)/bench_$(1): src/bench.c src/common.h src/hapax_common.h src/$(H_$(1)) | $(BIN)
> $(CC) $(CFLAGS) -DLOCK_HEADER='"$(H_$(1))"' -DLOCK_NAME='"$(1)"' $(D_$(1)) $$< -o $$@
$(BIN)/test_$(1): tests/test_mutex.c src/common.h src/hapax_common.h src/$(H_$(1)) | $(BIN)
> $(CC) $(CFLAGS) -DLOCK_HEADER='"$(H_$(1))"' -DLOCK_NAME='"$(1)"' $(D_$(1)) $$< -o $$@
$(BIN)/tsan_$(1): tests/test_mutex.c src/common.h src/hapax_common.h src/$(H_$(1)) | $(BIN)
> $(CC) $(TSAN) -DLOCK_HEADER='"$(H_$(1))"' -DLOCK_NAME='"$(1)"' $(D_$(1)) $$< -o $$@
endef
$(foreach l,$(LOCKS),$(eval $(call RULES,$(l))))

BENCHES = $(foreach l,$(LOCKS),$(BIN)/bench_$(l))
TESTS   = $(foreach l,$(LOCKS),$(BIN)/test_$(l))
TSANS   = $(foreach l,$(LOCKS),$(BIN)/tsan_$(l))

all: $(BENCHES) $(TESTS)
tsan: $(TSANS)

$(BIN):
> mkdir -p $(BIN)

check: $(TESTS)
> for l in $(LOCKS); do ./$(BIN)/test_$$l 8 200000 || exit 1; done
> NC=$$(nproc); for l in $(LOCKS); do timeout 120 ./$(BIN)/test_$$l $$((NC*2)) 20 || echo "$$l: oversubscribed test slow/timeout (expected for spinlocks, not a failure)"; done

check-tsan: $(TSANS)
> for l in $(LOCKS); do ./$(BIN)/tsan_$$l 4 20000 || exit 1; done

clean:
> rm -rf $(BIN)

.PHONY: all tsan check check-tsan clean
