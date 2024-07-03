#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../include/daemon.h"
#include "../include/ringbuf.h"

/* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU
 * changing the code will result in points deduction */

/********************************************************************
 * NETWORK TRAFFIC SIMULATION:
 * This section simulates incoming messages from various ports using
 * files. Think of these input files as data sent by clients over the
 * network to our computer. The data isn't transmitted in a single
 * large file but arrives in multiple small packets. This concept
 * is discussed in more detail in the advanced module:
 * Rechnernetze und Verteilte Systeme
 *
 * To simulate this parallel packet-based data transmission, we use multiple
 * threads. Each thread reads small segments of the files and writes these
 * smaller packets into the ring buffer. Between each packet, the
 * thread sleeps for a random time between 1 and 100 us. This sleep
 * simulates that data packets take varying amounts of time to arrive.
 *********************************************************************/
typedef struct {
  rbctx_t *ctx;
  connection_t *connection;
} w_thread_args_t;

void *write_packets(void *arg) {
  /* extract arguments */
  rbctx_t *ctx = ((w_thread_args_t *)arg)->ctx;
  size_t from = (size_t)((w_thread_args_t *)arg)->connection->from;
  size_t to = (size_t)((w_thread_args_t *)arg)->connection->to;
  char *filename = ((w_thread_args_t *)arg)->connection->filename;

  /* open file */
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file with name %s\n", filename);
    exit(1);
  }

  /* read file in chunks and write to ringbuffer with random delay */
  unsigned char buf[MESSAGE_SIZE];
  size_t packet_id = 0;
  size_t read = 1;
  while (read > 0) {
    size_t msg_size = MESSAGE_SIZE - 3 * sizeof(size_t);
    read = fread(buf + 3 * sizeof(size_t), 1, msg_size, fp);
    if (read > 0) {
      memcpy(buf, &from, sizeof(size_t));
      memcpy(buf + sizeof(size_t), &to, sizeof(size_t));
      memcpy(buf + 2 * sizeof(size_t), &packet_id, sizeof(size_t));
      while (ringbuffer_write(ctx, buf, read + 3 * sizeof(size_t)) != SUCCESS) {
        usleep(((rand() % 50) +
                25)); // sleep for a random time between 25 and 75 us
      }
    }
    packet_id++;
    usleep(((rand() % (100 - 1)) +
            1)); // sleep for a random time between 1 and 100 us
  }
  fclose(fp);
  return NULL;
}

/********************************************************************/

pthread_mutex_t filter_mutex = PTHREAD_MUTEX_INITIALIZER;
// Filtering function
int should_filter(size_t from_port, size_t to_port, char *message_content) {
  int filter = 0;
  if (from_port == 42 || to_port == 42 || (from_port + to_port) == 42) {
    filter = 1;
  }
  if (message_content != NULL) {
    // Check for "malicious" with possible characters in between
    pthread_mutex_lock(&filter_mutex);
    const char *pattern = "malicious";
    size_t pattern_index = 0;

    for (size_t i = 0; message_content[i] != '\0'; i++) {
      if (message_content[i] == pattern[pattern_index]) {
        pattern_index++;
        if (pattern[pattern_index] == '\0') {
          // Full pattern matched!
          filter = 1; // Filter out
        }
      }
    }
      pthread_mutex_unlock(&filter_mutex);
    }
    return filter;
  }

  pthread_mutex_t file_mutexes[MAXIMUM_PORT];
  char *destination_file_names[MAXIMUM_PORT];
  pthread_mutex_t ring_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t processing_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t write_condition[MAXIMUM_PORT] = PTHREAD_COND_INITIALIZER;
  int is_writing[MAXIMUM_PORT] = {0};

  // File writing function
  void write_to_file(size_t port, const char *message_content,
                     const size_t message_len) {
    pthread_mutex_lock(&file_mutexes[port]);

  //Waiting until no other thread is writing to this port
  while(is_writing[port]){
      pthread_cond_wait(&write_condition[port],&file_mutexes[port]);
    }
  is_writing[port] = 1;
    FILE *fp = fopen(destination_file_names[port], "a");

    if (fp != NULL) {
      printf("[Thread %ld] Writing %zu bytes to file %s\n", pthread_self(),
             message_len, destination_file_names[port]);
      size_t bytes_written = fwrite(message_content, 1, message_len, fp);
      if (bytes_written != message_len) {
        printf("[Thread %ld] ERROR: Wrote only %zu bytes (expected %zu) to file "
               "%s\n",
               pthread_self(), bytes_written, message_len,
               destination_file_names[port]);
      }
      fclose(fp);
    } else {
      printf("[Thread %ld] ERROR: Could not open file %s\n", pthread_self(),
             destination_file_names[port]);
    }
    is_writing[port]  = 0;
    pthread_cond_broadcast(&write_condition[port]);
    pthread_mutex_unlock(&file_mutexes[port]);
  }

  void *read_packets(void *arg) {
    rbctx_t *ctx = (rbctx_t *)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    unsigned char buf[MESSAGE_SIZE];
    while (1) {
      pthread_testcancel();

      size_t read_len = MESSAGE_SIZE;

      pthread_mutex_lock(&ring_buffer_mutex);
      int result = ringbuffer_read(ctx, buf, &read_len);
      pthread_mutex_unlock(&ring_buffer_mutex);

      if (result == RINGBUFFER_EMPTY) {
        continue;
      } else if (result == -1) {
        // Handle buffer too small condition
        continue;
      }

      pthread_mutex_lock(&processing_mutex);
      // Extract metadata from the packet
      size_t from_port, to_port, packet_id;
      memcpy(&from_port, buf, sizeof(size_t));
      memcpy(&to_port, buf + sizeof(size_t), sizeof(size_t));
      memcpy(&packet_id, buf + 2 * sizeof(size_t), sizeof(size_t));

      // Extract message content
      char *message_content = (char *)(buf + 3 * sizeof(size_t));
      size_t message_len = read_len - 3 * sizeof(size_t);
      // Implement filtering
      if (should_filter(from_port, to_port, message_content)) {
        // Discard message based on filtering criteria
        pthread_mutex_unlock(&processing_mutex);
        continue;
      }
      pthread_mutex_unlock(&processing_mutex);

      write_to_file(to_port, message_content, message_len);
      // Write message content to destination file
    }

    return NULL;
}

/* YOUR CODE ENDS HERE */

/********************************************************************/

int simpledaemon(connection_t *connections, int nr_of_connections) {
  /* initialize ringbuffer */
  rbctx_t rb_ctx;
  size_t rbuf_size = 1024;
  void *rbuf = malloc(rbuf_size);
  if (rbuf == NULL) {
    fprintf(stderr, "Error allocation ringbuffer\n");
  }

  ringbuffer_init(&rb_ctx, rbuf, rbuf_size);

  /****************************************************************
   * WRITER THREADS
   * ***************************************************************/

  /* prepare writer thread arguments */
  w_thread_args_t w_thread_args[nr_of_connections];
  for (int i = 0; i < nr_of_connections; i++) {
    w_thread_args[i].ctx = &rb_ctx;
    w_thread_args[i].connection = &connections[i];
    /* guarantee that port numbers range from MINIMUM_PORT (0) - MAXIMUMPORT */
    if (connections[i].from > MAXIMUM_PORT ||
        connections[i].to > MAXIMUM_PORT ||
        connections[i].from < MINIMUM_PORT ||
        connections[i].to < MINIMUM_PORT) {
      fprintf(stderr, "Port numbers %d and/or %d are too large\n",
              connections[i].from, connections[i].to);
      exit(1);
    }
  }

  /* start writer threads */
  pthread_t w_threads[nr_of_connections];
  for (int i = 0; i < nr_of_connections; i++) {
    pthread_create(&w_threads[i], NULL, write_packets, &w_thread_args[i]);
  }

  /****************************************************************
   * READER THREADS
   * ***************************************************************/

  pthread_t r_threads[NUMBER_OF_PROCESSING_THREADS];

  /* END OF PROVIDED CODE */

  /********************************************************************/

  /* YOUR CODE STARTS HERE */
  for (int i = 0; i < MAXIMUM_PORT; ++i) {
    pthread_mutex_init(&file_mutexes[i], NULL);
    destination_file_names[i] = malloc(256);
    sprintf(destination_file_names[i], "%d.txt", i);
  }

  // STARTING READER THREADS

  for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
    pthread_create(&r_threads[i], NULL, read_packets, &rb_ctx);
  }

  /* YOUR CODE ENDS HERE */

  /********************************************************************/

  /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU
   * changing the code will result in points deduction */

  /****************************************************************
   * CLEANUP
   * ***************************************************************/

  /* after 5 seconds JOIN all threads (we should definitely have received all
   * messages by then) */
  printf("daemon: waiting for 5 seconds before canceling reading threads\n");
  sleep(5);
  for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
    pthread_cancel(r_threads[i]);
  }
  /* wait for all threads to finish */
  for (int i = 0; i < nr_of_connections; i++) {
    pthread_join(w_threads[i], NULL);
  }
  /* join all threads */
  for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
    pthread_join(r_threads[i], NULL);
  }
  /* END OF PROVIDED CODE */

  /********************************************************************/

  /* YOUR CODE STARTS HERE */

  for (int i = 0; i < MAXIMUM_PORT; ++i) {
    pthread_mutex_destroy(&file_mutexes[i]);
    free(destination_file_names[i]);
  }

  pthread_mutex_destroy(&ring_buffer_mutex);
  pthread_mutex_destroy(&filter_mutex);
  pthread_mutex_destroy(&processing_mutex);
  /* YOUR CODE ENDS HERE */

  /********************************************************************/

  /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU
   * changing the code will result in points deduction */

  free(rbuf);
  ringbuffer_destroy(&rb_ctx);

  return 0;

  /* END OF PROVIDED CODE */
}
