#include <stdio.h>
#include <stdlib.h>
#include "pthread.h"

#include "filter.h"
#include "pipeline.h"
#include "queue.h"

#define NUM_STEPS 4
#define NUM_PARALLEL_THREADS 4
#define NUM_QUEUES 4
#define QUEUE_SIZE 500


// Global variables
queue_t **queues;
pthread_mutex_t mutex;
bool complete = false;

void* read_all_images(void* args) {
	image_dir_t* image_dir = (image_dir_t*) args;

	while (1) {
		image_t* image = image_dir_load_next(image_dir);
		queue_push(queues[0], image);
		if (image == NULL) {
			printf("Finished reading images\n");
			break;
		}
	}
	return 0;
}

void* scale_up(void* args) {
	image_t* image = (image_t*) queue_pop(queues[0]);
	if (image == NULL) {
		queue_push(queues[0], NULL);
		if(!complete) {
			queue_push(queues[1], NULL);
		}
		return 0;
	}
	image_t* modified = filter_scale_up(image, 2);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished scaling image\n");
	image_destroy(image);
	queue_push(queues[1], modified);
	return 0;
}

void* sharpen(void* args) {
	image_t* image = (image_t*) queue_pop(queues[1]);
	if (image == NULL) {
		queue_push(queues[1], NULL);
		
		if(!complete) {
			queue_push(queues[2], NULL);
		}
		return 0;
	}
	image_t* modified = filter_sharpen(image);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished sharpening images\n");
	image_destroy(image);
	queue_push(queues[2], modified);
	return 0;
}

void* sobel(void* args) {
	image_t* image = (image_t*) queue_pop(queues[2]);
	if (image == NULL) {
		queue_push(queues[2], NULL);
		
		if(!complete) {
			queue_push(queues[3], NULL);
		}
		return 0;
	}
	image_t* modified = filter_sobel(image);
	if(modified == NULL) {
		exit(-1);
	}
	printf("Finished sobel images\n");
	image_destroy(image);
	queue_push(queues[3], modified);
	return 0;
}

void* save_all_images(void* args) {
	image_dir_t *image_dir = (image_dir_t*) args;
	image_t* image = (image_t*) queue_pop(queues[3]);
	if (image == NULL) {
		pthread_mutex_lock(&mutex);
		complete = true;
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

int pipeline_pthread(image_dir_t* image_dir) {
	// Allocate resources
	if(pthread_mutex_init(&mutex, NULL) != 0) {
		printf("Mutex init failed\n");
		return -1;
	}
	queues = calloc(NUM_QUEUES, sizeof(queue_t*));
	for(int i = 0; i < NUM_QUEUES; i++) {
		queues[i] = queue_create(QUEUE_SIZE);
	}
	pthread_t read_tid;
	if(pthread_create(&read_tid, NULL, read_all_images, image_dir) != 0) {
		printf("Read image pipeline failed\n");
		return -1;
	}

	pthread_t tids[NUM_PARALLEL_THREADS][NUM_STEPS] = {0};
	while (!complete) {
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

	// Free resources
	for(int i = 0; i < NUM_QUEUES; i++) {
		queue_destroy(queues[i]);
	}
	free(queues);
	pthread_mutex_destroy(&mutex);
	
	return 0;
}
