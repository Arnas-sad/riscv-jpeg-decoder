#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <xil_printf.h>
#include <myfifo.h>

// do not change this function
// but feel free to add your own fifo_print_debug that prints more information

void myfifo_print_status(myfifo_t volatile const * const fifo_admin)
{
  xil_printf("spaces=%u tokens=%u\n", myfifo_spaces(fifo_admin), myfifo_tokens(fifo_admin));
  
}


// token_buffer must point to an array containing nr_tokens
// function returns NULL on failure and fifo_admin on success
myfifo_t * myfifo_init(myfifo_t volatile * const fifo_admin, void volatile * const token_buffer, uint32_t const nr_tokens, uint32_t const token_size) {

  if (fifo_admin == NULL || token_buffer == NULL || nr_tokens == 0 || token_size == 0)
      return NULL;
  fifo_admin->size = token_size;
  fifo_admin->tail = 0;
  fifo_admin->head = 0;
  fifo_admin->nr_tokens = nr_tokens;
  fifo_admin->token_size = token_size;
  fifo_admin->token_buffer = token_buffer;
  fifo_admin->initialized = MYFIFO_INITIALIZED_MAGIC;  // written last
  
  
  return (myfifo_t *) fifo_admin;
  
}
void myfifo_destroy(myfifo_t volatile * const fifo_admin)
{
// myfifo_initialized must return false
// - before myfifo_init has been called successfully, or
// - after myfifo_destroy has been called
// otherwise it returns true
  fifo_admin->initialized = 0;
}

bool myfifo_initialized(myfifo_t volatile const * const fifo_admin)
{
// do not change myfifo_print_status

  return fifo_admin->initialized == MYFIFO_INITIALIZED_MAGIC;
}


uint32_t myfifo_spaces(myfifo_t volatile const * const fifo_admin)
{
  return fifo_admin->nr_tokens - (fifo_admin->tail - fifo_admin->head);
}
  
  

uint32_t myfifo_tokens(myfifo_t volatile const * const fifo_admin)
{
// the claim/release functions are blocking,
// i.e. return when space/token has been claimed/released
// the claim functions return a pointer to the space/token
  return fifo_admin->tail - fifo_admin->head;
}

void volatile * myfifo_claim_space(myfifo_t volatile * const fifo_admin)
{
  while (fifo_admin->tail - fifo_admin->head == fifo_admin->nr_tokens)
    asm("wfi");

  uint32_t slot = fifo_admin->tail % fifo_admin->nr_tokens;
  return (void volatile *)((uint8_t volatile *)fifo_admin->token_buffer + slot * fifo_admin->token_size);
}


void myfifo_release_token(myfifo_t volatile * const fifo_admin)
{
  fifo_admin->tail++;
}


void volatile * myfifo_claim_token(myfifo_t volatile * const fifo_admin)
{
  while (fifo_admin->tail == fifo_admin->head)
    asm("wfi");
    
  uint32_t slot = fifo_admin->head % fifo_admin->nr_tokens;
  return (void volatile *)((uint8_t volatile *)fifo_admin->token_buffer + slot * fifo_admin->token_size);
}


void myfifo_release_space(myfifo_t volatile * const fifo_admin)
{
  fifo_admin->head++;
}

// blocking functions; write/read = claim + memcpy + release
// can only write/read when no spaces/tokens have been claimed
void myfifo_write_token(myfifo_t volatile * const fifo_admin, void const * const token_to_write)
{
  uint8_t volatile * dst = (uint8_t volatile *) myfifo_claim_space(fifo_admin);
  uint8_t const    * src = (uint8_t const *)    token_to_write;

  for (uint32_t i = 0; i < fifo_admin->token_size; i++)
    dst[i] = src[i];

  myfifo_release_token(fifo_admin);
}

void myfifo_read_token(myfifo_t volatile * const fifo_admin, void * const new_token)
{
  uint8_t volatile * src = (uint8_t volatile *) myfifo_claim_token(fifo_admin);
  uint8_t          * dst = (uint8_t *)          new_token;

  for (uint32_t i = 0; i < fifo_admin->token_size; i++)
    dst[i] = src[i];

  myfifo_release_space(fifo_admin);
}
// your functions
