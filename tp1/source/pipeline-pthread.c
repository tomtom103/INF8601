#include <stdio.h>
#include <stdlib.h>
#include "pthread.h"

#include "filter.h"
#include "pipeline.h"
#include "queue.h"
#include "log.h"

#define NUM_STEPS 4
#define NUM_PARALLEL_THREADS 4
#define NUM_QUEUES 4
#define QUEUE_SIZE 500


// Global variables
queue_t **queues = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
bool read_done = false;
bool finished = false;

void* read_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;

	while (1) {
		image_t* image = image_dir_load_next(image_dir);
		queue_push(queues[0], image);
		if (image == NULL) {
			pthread_mutex_lock(&mutex);
			read_done = true;
			pthread_cond_broadcast(&cond);
			pthread_mutex_unlock(&mutex);
			break;
		}
	}
	return 0;
}

void* scale_up(void* args) {
	image_t* image = (image_t*) queue_pop(queues[0]);
	if (image == NULL) {
		queue_push(queues[1], NULL);
		return 0;
	}
	image_t* modified = filter_scale_up(image, 2);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished scaling image\n");
	image_destroy(image);
	queue_push(queues[1], modified);
}

void* sharpen(void* args) {
	image_t* image = (image_t*) queue_pop(queues[1]);
	if (image == NULL) {
		queue_push(queues[2], NULL);
		return 0;
	}
	image_t* modified = filter_sharpen(image);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished sharpening images\n");
	image_destroy(image);
	queue_push(queues[2], modified);
}

void* sobel(void* args) {
	image_t* image = (image_t*) queue_pop(queues[2]);
	if (image == NULL) {
		queue_push(queues[3], NULL);
		return 0;
	}
	image_t* modified = filter_sobel(image);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished sobel images\n");
	image_destroy(image);
	queue_push(queues[3], modified);
}

void* save_all_images(void* args) {
	image_dir_t *image_dir = (image_dir_t*) args;
	image_t* image = (image_t*) queue_pop(queues[3]);
	if (image == NULL) {
		pthread_mutex_lock(&mutex);
		finished = true;
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&mutex);
		return 0;
	}
	printf("Saving images\n");
	printf(".");
	fflush(stdout);
	image_dir_save(image_dir, image);
	image_destroy(image);
	return 0;
}

void* verify_queues(void* args) {
	pthread_mutex_lock(&mutex);
	while (!read_done) {
		pthread_cond_wait(&cond, &mutex);
	}
	pthread_mutex_unlock(&mutex);

	for(int i = 0; i < NUM_PARALLEL_THREADS; i++) {
		// Signal threads to finish
		queue_push(queues[0], NULL);
	}

	pthread_mutex_lock(&mutex);
	while (!finished) {
		pthread_cond_wait(&cond, &mutex);
	}
	pthread_mutex_unlock(&mutex);
	return 0;
}

int pipeline_pthread(image_dir_t* image_dir) {
	queues = calloc(NUM_QUEUES, sizeof(queue_t*));
	if(queues == NULL) {
		LOG_ERROR_ERRNO("Failed to allocate memory for queues");
		goto fail_exit;
	}
	for(int i = 0; i < NUM_QUEUES; i++) {
		queues[i] = queue_create(QUEUE_SIZE);
	}

	errno = pthread_mutex_init(&mutex, NULL);
	if(errno != 0) {
		LOG_ERROR_ERRNO("Mutex init failed");
		goto fail_free_queues;
	}

	errno = pthread_cond_init(&cond, NULL);
	if(errno != 0) {
		LOG_ERROR_ERRNO("Cond init failed");
		goto fail_destroy_mutex;
	}

	pthread_t read_tid;
	errno = pthread_create(&read_tid, NULL, read_all_images, image_dir);
	if(errno != 0) {
		LOG_ERROR_ERRNO("Failed to create read thread");
		goto fail_destroy_cond;
	}

	pthread_t verify_queues_tid;
	errno = pthread_create(&verify_queues_tid, NULL, verify_queues, NULL);
	if(errno != 0) {
		LOG_ERROR_ERRNO("Failed to create verify queues thread");
		goto fail_destroy_cond;
	}

	pthread_t tids[NUM_PARALLEL_THREADS][NUM_STEPS] = {0};
	while (!finished) {
		for(int i = 0; i < NUM_PARALLEL_THREADS; i++) {
			pthread_create(&tids[i][0], NULL, scale_up, NULL);
			pthread_create(&tids[i][1], NULL, sharpen, NULL);
			pthread_create(&tids[i][2], NULL, sobel, NULL);
			pthread_create(&tids[i][3], NULL, save_all_images, image_dir);
		}

		for(int i = 0; i < NUM_PARALLEL_THREADS; i++) {
			for(int j = 0; j < NUM_STEPS; j++) {
				pthread_join(tids[i][j], NULL);
			}
		}
	}

	pthread_join(read_tid, NULL);
	pthread_join(verify_queues_tid, NULL);

	// Free resources
	for(int i = 0; i < NUM_QUEUES; i++) {
		queue_destroy(queues[i]);
	}
	free(queues);
	pthread_mutex_destroy(&mutex);
	
	return 0;

fail_destroy_cond:
	pthread_cond_destroy(&cond);
fail_destroy_mutex:
	pthread_mutex_destroy(&mutex);
fail_free_queues:
	for(int i = 0; i < NUM_QUEUES; i++) {
		queue_destroy(queues[i]);
	}
	free(queues);
fail_exit:
	return -1;
}
