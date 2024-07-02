#include "../include/ringbuf.h"
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{
  context->begin = (uint8_t *)buffer_location;
  context->end = (uint8_t *)buffer_location + buffer_size;
  context->read = context->begin; 
  context->write = context->begin;

  pthread_mutex_init(&context->mutex_read, NULL);
  pthread_mutex_init(&context->mutex_write, NULL);
 
  pthread_cond_init(&context->signal_read, NULL);
  pthread_cond_init(&context->signal_write, NULL);
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len)
{
    /* your solution here */
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len)
{
    /* your solution here */
}

void ringbuffer_destroy(rbctx_t *context)
{
    /* your solution here */
}
