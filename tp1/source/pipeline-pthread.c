#include <stdio.h>
#include <stdlib.h>
#include "pthread.h"

#include "filter.h"
#include "pipeline.h"
#include "queue.h"

#define NUM_THREADS 16
#define QUEUE_SIZE 400

queue_t *scale_up_queue, *sharpen_queue, *sobel_queue, *save_queue;
pthread_mutex_t mutex;

void* read_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;

	while (1) {
		image_t* image = image_dir_load_next(image_dir);
		queue_push(scale_up_queue, image);
		if (image == NULL) {
			break;
		}
	}
}

void* scale_up(void* args) {
	image_t* image = (image_t*) queue_pop(scale_up_queue);
	if (image == NULL) {
		queue_push(sharpen_queue, NULL);
		return NULL;
	}
	image_t* modified = filter_scale_up(image, 2);
	image_destroy(image);
	queue_push(sharpen_queue, modified);
}

void* sharpen(void* args) {
	image_t* image = (image_t*) queue_pop(sharpen_queue);
	if (image == NULL) {
		queue_push(sobel_queue, NULL);
		return NULL;
	}
	image_t* modified = filter_sharpen(image);
	image_destroy(image);
	queue_push(sobel_queue, modified);
}

void* sobel(void* args) {
	image_t* image = (image_t*) queue_pop(sobel_queue);
	if (image == NULL) {
		queue_push(save_queue, NULL);
		return NULL;
	}
	image_t* modified = filter_sobel(image);
	image_destroy(image);
	queue_push(save_queue, modified);
}

void* save_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;
	image_t* image = (image_t*) queue_pop(save_queue);
	if (image == NULL) {
		exit(-1);
	}
	pthread_mutex_lock(&mutex);
	image_dir_save(image_dir, image);
	pthread_mutex_unlock(&mutex);
	printf(".");
	fflush(stdout);
	image_destroy(image);
}

void process_images(image_dir_t* image_dir) {
	pthread_t threads[4][NUM_THREADS];

	while (1) {
		for(int i = 0; i < NUM_THREADS; i++) {
			pthread_create(&threads[0][i], NULL, scale_up, NULL);
		}
		for(int i = 0; i < NUM_THREADS; i++) {
			pthread_create(&threads[1][i], NULL, sharpen, NULL);
		}
		for(int i = 0; i < NUM_THREADS; i++) {
			pthread_create(&threads[2][i], NULL, sobel, NULL);
		}
		for(int i = 0; i < NUM_THREADS; i++) {
			pthread_create(&threads[3][i], NULL, save_all_images, image_dir);
		}
		
		for(int j = 0; j < 4; j++) {
			for(int i = 0; i < NUM_THREADS; i++) {
				int rc = pthread_join(threads[j][i], NULL);
				if(j==3) {
					if (rc == -1) {
						return;
					}
				}
			}
		}

	}
}

int pipeline_pthread(image_dir_t* image_dir) {
	pthread_mutex_init(&mutex, NULL);

	scale_up_queue = queue_create(QUEUE_SIZE);
	sharpen_queue = queue_create(QUEUE_SIZE);
	sobel_queue = queue_create(QUEUE_SIZE);
	save_queue = queue_create(QUEUE_SIZE);

	pthread_t read_thread;

	pthread_create(&read_thread, NULL, read_all_images, image_dir);

	process_images(image_dir);

	// Delete queues
	queue_destroy(scale_up_queue);
	queue_destroy(sharpen_queue);
	queue_destroy(sobel_queue);
	queue_destroy(save_queue);

	pthread_mutex_destroy(&mutex);
	
	return 0;
}
