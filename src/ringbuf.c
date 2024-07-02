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
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;

    pthread_mutex_lock(&context->mutex_write);

    size_t total_space = context->end - context->begin;
    size_t space_needed = message_len + sizeof(size_t);

    if (space_needed > total_space) {
        pthread_mutex_unlock(&context->mutex_write);
        return RINGBUFFER_FULL;
    }

    size_t available_space;
    if (context->write >= context->read) {
        available_space = (context->end - context->write) + (context->read - context->begin) - 1;
    } else {
        available_space = context->read - context->write - 1;
    }

    while (available_space < space_needed) {
        int res = pthread_cond_timedwait(&context->signal_write, &context->mutex_write, &timeout);
        if (res == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mutex_write);
            return RBUF_TIMEOUT;
        }

        if (context->write >= context->read) {
            available_space = (context->end - context->write) + (context->read - context->begin) - 1;
        } else {
            available_space = context->read - context->write - 1;
        }
    }

    if (context->write + sizeof(size_t) <= context->end) {
        memcpy(context->write, &message_len, sizeof(size_t));
        context->write += sizeof(size_t);

        if (context->write == context->end) {
            context->write = context->begin;
        }
    } else {
        size_t first_part_size = context->end - context->write;
        memcpy(context->write, &message_len, first_part_size);
        memcpy(context->begin, (uint8_t *)&message_len + first_part_size, sizeof(size_t) - first_part_size);
        context->write = context->begin + (sizeof(size_t) - first_part_size);
    }

    if (context->write + message_len <= context->end) {
        memcpy(context->write, message, message_len);
        context->write += message_len;

        if (context->write == context->end) {
            context->write = context->begin;
        }
    } else {
        size_t first_part_size = context->end - context->write;
        memcpy(context->write, message, first_part_size);
        memcpy(context->begin, (uint8_t *)message + first_part_size, message_len - first_part_size);
        context->write = context->begin + (message_len - first_part_size);
    }

    pthread_cond_signal(&context->signal_read);
    pthread_mutex_unlock(&context->mutex_write);
    return SUCCESS;
}

size_t read_message_size_from_ringbuffer(rbctx_t *context) {
    size_t value;
    uint8_t *read_ptr = context->read;
    size_t size_t_size = sizeof(size_t);

    if (read_ptr + size_t_size <= context->end) {
        memcpy(&value, read_ptr, size_t_size);
    } else {
        size_t first_part_size = context->end - read_ptr;
        uint8_t temp_buffer[size_t_size];

        memcpy(temp_buffer, read_ptr, first_part_size);
        memcpy(temp_buffer + first_part_size, context->begin, size_t_size - first_part_size);
        memcpy(&value, temp_buffer, size_t_size);
    }

    context->read += size_t_size;
    if (context->read >= context->end) {
        context->read = context->begin + (context->read - context->end);
    }

    return value;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len) {
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;

    pthread_mutex_lock(&context->mutex_read);
    while (context->read == context->write) {
        int res = pthread_cond_timedwait(&context->signal_read, &context->mutex_read, &timeout);
        if (res == ETIMEDOUT) {
            pthread_mutex_unlock(&context->mutex_read);
            return RINGBUFFER_EMPTY;
        }
    }

    size_t size = read_message_size_from_ringbuffer(context);

    if (*buffer_len < size) {
        pthread_mutex_unlock(&context->mutex_read);
        return -1; // Buffer too small
    }

    if (context->read + size <= context->end) {
        memcpy(buffer, context->read, size);
        context->read += size;
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
    return SUCCESS;
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
