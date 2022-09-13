#include <stdio.h>
#include "pthread.h"

#include "filter.h"
#include "pipeline.h"
#include "queue.h"

#define NUM_STEPS 4
#define NUM_THREADS 16
#define QUEUE_SIZE 400

const queue_t *scale_up_queue, *sharpen_queue, *sobel_queue, *save_queue;
pthread_mutex_t* mutex;
pthread_cond_t* pcond;

void* read_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;

	while (1) {
		pthread_mutex_lock(mutex);
		image_t* image = image_dir_load_next(image_dir);
		pthread_mutex_unlock(mutex);

		if (image == NULL) {
			pthread_cond_signal(pcond);
			break;
		}
		queue_push(scale_up_queue, image);
	}
}


void* save_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;
	while (1) {
		image_t* image = (image_t*) queue_pop(save_queue);
		if (image == NULL) {
			break;
		}
		pthread_mutex_lock(mutex);
		image_dir_save(image_dir, image);
		pthread_mutex_unlock(mutex);
		printf(".");
		fflush(stdout);
		image_destroy(image);
	}
}

void* scale_up(void* args) {
	image_t* image = (image_t*) queue_pop(scale_up_queue);
	if (image == NULL) {
		return NULL;
	}
	image_t* modified = filter_scale_up(image, 2);
	image_destroy(image);
	queue_push(sharpen_queue, modified);
}

void* sharpen(void* args) {
	image_t* image = (image_t*) queue_pop(sharpen_queue);
	if (image == NULL) {
		return NULL;
	}
	image_t* modified = filter_sharpen(image);
	image_destroy(image);
	queue_push(sobel_queue, modified);
}

void* sobel(void* args) {
	image_t* image = (image_t*) queue_pop(sobel_queue);
	if (image == NULL) {
		return NULL;
	}
	image_t* modified = filter_sobel(image);
	image_destroy(image);
	queue_push(save_queue, modified);
}


int pipeline_pthread(image_dir_t* image_dir) {
	pthread_mutex_init(mutex, NULL);
	pthread_cond_init(pcond, NULL);

	scale_up_queue = queue_create(QUEUE_SIZE);
	sharpen_queue = queue_create(QUEUE_SIZE);
	sobel_queue = queue_create(QUEUE_SIZE);
	save_queue = queue_create(QUEUE_SIZE);

	pthread_t threads[4][NUM_THREADS];

	pthread_t read_thread;
	pthread_t save_thread;

	pthread_create(&read_thread, NULL, read_all_images, image_dir);

	while (1) {
		// TODO:
	}

	pthread_mutex_destroy(mutex);
	
	return 0;
}
