#include "../include/ringbuf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{
    /* your solution here */
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
