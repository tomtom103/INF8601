#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include "log.h"
#include "sinoscope.h"

int sinoscope_opencl_init(sinoscope_opencl_t* opencl, cl_device_id opencl_device_id, unsigned int width,
			  unsigned int height) {
	cl_int error = 0;
	opencl->device_id = opencl_device_id;
	opencl->context = clCreateContext(0, 1, &opencl_device_id, NULL, NULL, &error);
	if (error != CL_SUCCESS) return -1;
	opencl->queue = clCreateCommandQueue(opencl->context, opencl_device_id, 0, &error);
	if (error != CL_SUCCESS) return -1;
	return 0;
}

void sinoscope_opencl_cleanup(sinoscope_opencl_t* opencl)
{
	
}

int sinoscope_image_opencl(sinoscope_t* sinoscope) {
	return -1;
}
