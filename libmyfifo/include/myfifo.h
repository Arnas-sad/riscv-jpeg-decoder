#ifndef _MYFIFOH_
#define _MYFIFOH_

#include <stdint.h>
#include <stdbool.h>

// Magic value written last by myfifo_init() and cleared by myfifo_destroy().
// Using a magic value instead of 1 makes myfifo_initialized() safe even when
// admin memory is not zeroed (e.g. when placed in DRAM).
#define MYFIFO_INITIALIZED_MAGIC 0xCAFEBEEFu

typedef volatile struct _myfifo_t {
  uint32_t size;
  uint32_t tail;
  uint32_t head;
  uint32_t nr_tokens;
  uint32_t token_size;
  uint32_t initialized;  // MYFIFO_INITIALIZED_MAGIC when ready, 0 otherwise
  void volatile * token_buffer;
} myfifo_t;

// token_buffer must point to an array containing nr_tokens
// function returns NULL on failure and fifo_admin on success
extern myfifo_t * myfifo_init(myfifo_t volatile * const fifo_admin, void volatile * const token_buffer, uint32_t const nr_tokens, uint32_t const token_size);
extern void myfifo_destroy(myfifo_t volatile * const fifo_admin);
// myfifo_initialized must return false
// - before myfifo_init has been called successfully, or
// - after myfifo_destroy has been called
// otherwise it returns true
extern bool myfifo_initialized(myfifo_t volatile const * const fifo_admin);
// do not change myfifo_print_status
extern void myfifo_print_status(myfifo_t volatile const * const fifo_admin);
// use myfifo_spaces/tokens to poll, for non-blocking behaviour
// myfifo_spaces/tokens returns the number of spaces/tokens that can still be claimed
extern uint32_t myfifo_spaces(myfifo_t volatile const * const fifo_admin);
extern uint32_t myfifo_tokens(myfifo_t volatile const * const fifo_admin);
// the claim/release functions are blocking,
// i.e. return when space/token has been claimed/released
// the claim functions return a pointer to the space/token
extern void volatile * myfifo_claim_space(myfifo_t volatile * const fifo_admin);
extern void myfifo_release_token(myfifo_t volatile * const fifo_admin);
extern void volatile * myfifo_claim_token(myfifo_t volatile * const fifo_admin);
extern void myfifo_release_space(myfifo_t volatile * const fifo_admin);
// blocking functions; write/read = claim + memcpy + release
// can only write/read when no spaces/tokens have been claimed
extern void myfifo_write_token(myfifo_t volatile * const fifo_admin, void const * const token_to_write);
extern void myfifo_read_token(myfifo_t volatile * const fifo_admin, void * const new_token);

#endif

