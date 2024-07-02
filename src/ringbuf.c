#include "../include/ringbuf.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location,
                     size_t buffer_size) {
  context->begin = (uint8_t *)buffer_location;
  context->end = (uint8_t *)buffer_location + buffer_size;
  context->read = context->begin;
  context->write = context->begin;

  pthread_mutex_init(&context->mutex_read, NULL);
  pthread_mutex_init(&context->mutex_write, NULL);

  pthread_cond_init(&context->signal_read, NULL);
  pthread_cond_init(&context->signal_write, NULL);
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len) {
  pthread_mutex_lock(&context->mutex_write);

  size_t total_space = context->end - context->begin;
  size_t available_space = context->end - context->write;
  size_t space_needed = message_len + sizeof(size_t);

  if (space_needed > total_space)
    return RINGBUFFER_FULL;

  // check if there is Enough space before the end of the buffer
  if (available_space >= space_needed) {
    // write the length of the message
    size_t *len_ptr = (size_t *)context->write;
    *len_ptr = message_len;
    context->write += sizeof(message_len);
    // write message data
    memcpy(context->write, message, message_len);
    context->write += message_len; // move write pointer to after the message

    if (context->write == context->end)
      context->write = context->begin;
    // signal that data has been written
    pthread_cond_signal(&context->signal_write);
  } else {
    // if the the available space less than the space we need to write
    // write message length
    if (available_space < sizeof(message_len)) {
      size_t first_chunk_size = available_space;
      size_t second_chunk_size = sizeof(message_len) - first_chunk_size;

      memcpy(context->write, &message_len, first_chunk_size);
      context->write = context->begin + second_chunk_size;
    } else {
      size_t *len_ptr = (size_t *)context->write;
      *len_ptr = message_len;
      context->write += message_len;
    }

    // recalculate space available
    available_space = context->end - context->write;

    if (available_space < message_len) {

      // write message data
      size_t first_chunk_size = available_space;
      size_t second_chunk_size = space_needed - available_space;

      // TODO: do not overtake read ptr, check this shit please
      memcpy(context->write, message, first_chunk_size);
      memcpy(context->begin, message + first_chunk_size, second_chunk_size);

      context->write = context->begin + second_chunk_size;
    } else {
      // write message data
      memcpy(context->write, message, message_len);
      context->write += message_len; // move write pointer to after the message
    }
    // signal that data has been written
    pthread_cond_signal(&context->signal_write);
  }

  pthread_mutex_unlock(&context->mutex_write);
  return SUCCESS;
}

size_t read_message_size_from_ringbuffer(rbctx_t *context) {
  size_t size;
  uint8_t *read_ptr = context->read;
  size_t size_t_size = sizeof(size_t);

  if (read_ptr + size_t_size <= context->end) {
    memcpy(&size, read_ptr, size_t_size);
  } else {
    size_t first_part_size = context->end - read_ptr;
    uint8_t temp_buffer[size_t_size];

    memcpy(temp_buffer, read_ptr, first_part_size);
    memcpy(temp_buffer + first_part_size, context->begin,
           size_t_size - first_part_size);

    memcpy(&size, temp_buffer, size_t_size);
  }
  context->read += size_t_size;
  if (context->read >= context->end) {
    context->read = context->begin + (context->read - context->end);
  }

  return size;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len) {
  pthread_mutex_lock(&context->mutex_read);
  while (context->read == context->write) {
    pthread_cond_wait(&context->signal_read, &context->mutex_read);
  }
  size_t size = read_message_size_from_ringbuffer(context);
  if (context->read + size <= context->end) {
    memcpy(buffer, context->read, size);
  } else {
    size_t first_part = context->end - context->read;
    size_t second_part = size - first_part;
    memcpy(buffer, context->read, first_part);
    memcpy((uint8_t *)buffer + first_part, context->begin, second_part);
    context->read = context->begin + second_part;
  }
  if (context->read == context->end) {
    context->read = context->begin;
  }

  pthread_cond_signal(&context->signal_write);
  pthread_mutex_unlock(&context->mutex_read);
  *buffer_len = size;
  return 0;
}

void ringbuffer_destroy(rbctx_t *context) {
  pthread_mutex_destroy(&context->mutex_read);
  pthread_mutex_destroy(&context->mutex_write);
  pthread_cond_destroy(&context->signal_write);
  pthread_cond_destroy(&context->signal_read);

  context->begin = NULL;
  context->end = NULL;
  context->write = NULL;
  context->read = NULL;
}
