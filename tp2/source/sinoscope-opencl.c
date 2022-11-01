#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include "log.h"
#include "sinoscope.h"

typedef struct __attribute__((packed)) sinoscope_opencl_args {
    cl_float interval_inverse;
    cl_float time;
    cl_float max;
    cl_float phase0;
    cl_float phase1;
    cl_float dx;
    cl_float dy;
} sinoscope_opencl_args_t;


int sinoscope_opencl_init(sinoscope_opencl_t* opencl, cl_device_id opencl_device_id, unsigned int width,
			  unsigned int height) {
	cl_int error = 0;
	opencl->device_id = opencl_device_id;

	opencl->context = clCreateContext(0, 1, &opencl_device_id, NULL, NULL, &error);
	if (error != CL_SUCCESS) return -1;

	opencl->queue = clCreateCommandQueue(opencl->context, opencl_device_id, 0, &error);
	if (error != CL_SUCCESS) return -1;

	
	opencl->buffer = clCreateBuffer(opencl->context, 
        CL_MEM_READ_WRITE | CL_MEM_HOST_READ_ONLY, 
        width * height * 9U, NULL, &error 
    );

    if (error != CL_SUCCESS) return -1;

    size_t size = 1111110;
    char* code = NULL;
    opencl_load_kernel_code(&code, &size);    
    if (error != CL_SUCCESS) LOG_ERROR("Error creating kernel: %i\n", (int)error);

    cl_program program = clCreateProgramWithSource(opencl->context, 1, (const char**) &code, &size, &error);
    if (error != CL_SUCCESS) return -1;
    
    error = clBuildProgram(program, 1, &(opencl->device_id), "-I " __OPENCL_INCLUDE__, NULL, NULL);

    if (error != CL_SUCCESS) {
        size_t logSize = 0;
        clGetProgramBuildInfo(program, opencl->device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);
        
        char *buffer = malloc(logSize + 1);
        buffer[logSize] = '\0';

        clGetProgramBuildInfo(program, opencl->device_id, CL_PROGRAM_BUILD_LOG, logSize, buffer, NULL);

        printf("Value of buildLog: %.*s\n", (int)sizeof(buffer), buffer);
    }


    opencl->kernel = clCreateKernel(program, "sinoscope_kernel", &error);
    if (error != CL_SUCCESS) LOG_ERROR("Error creating kernel: %i\n", (int)error);

	return 0;
}

void sinoscope_opencl_cleanup(sinoscope_opencl_t* opencl)
{
    if(opencl->kernel) clReleaseKernel(opencl->kernel);
    if(opencl->queue) clReleaseCommandQueue(opencl->queue);
    if(opencl->context) clReleaseContext(opencl->context);
    if(opencl->buffer) clReleaseMemObject(opencl->buffer);
}


int sinoscope_image_opencl(sinoscope_t* sinoscope) {
    if (sinoscope == NULL) {
        LOG_ERROR_NULL_PTR();
        goto fail_exit;
    }

    cl_int error;

    error = clSetKernelArg(sinoscope->opencl->kernel, 0, sizeof(cl_mem), &(sinoscope->opencl->buffer));
    if(error != CL_SUCCESS) LOG_ERROR("Error setting buffer arg: %i\n", (int)error);

    sinoscope_opencl_args_t args = {
        sinoscope->interval_inverse,
        sinoscope->time,
        sinoscope->max,
        sinoscope->phase0,
        sinoscope->phase1,
        sinoscope->dx,
        sinoscope->dy
    };

    error = clSetKernelArg(sinoscope->opencl->kernel, 1, sizeof(args), &args);
    if(error != CL_SUCCESS) LOG_ERROR("Error setting struct args: %i\n", (int)error);

    error = clSetKernelArg(sinoscope->opencl->kernel, 2, sizeof(cl_int), &sinoscope->width);
    if(error != CL_SUCCESS) LOG_ERROR("Error setting width arg: %i\n", (int)error);

    error = clSetKernelArg(sinoscope->opencl->kernel, 3, sizeof(cl_int), &sinoscope->height);
    if(error != CL_SUCCESS) LOG_ERROR("Error setting height arg: %i\n", (int)error);

    error = clSetKernelArg(sinoscope->opencl->kernel, 4, sizeof(cl_int), &sinoscope->taylor);
    if(error != CL_SUCCESS) LOG_ERROR("Error setting taylor arg: %i\n", (int)error);

    error = clSetKernelArg(sinoscope->opencl->kernel, 5, sizeof(cl_int), &sinoscope->interval);
    if(error != CL_SUCCESS) LOG_ERROR("Error setting interval arg: %i\n", (int)error);

    const size_t global_work_size = sinoscope->buffer_size; 

    error = clEnqueueNDRangeKernel(sinoscope->opencl->queue, sinoscope->opencl->kernel, 1, 
                                    NULL, &global_work_size, 0, 0, NULL, NULL);
    if(error != CL_SUCCESS) LOG_ERROR("Error in enqueue: %i\n", (int)error);

    // error =clFlush(sinoscope->opencl->queue);
    // if(error != CL_SUCCESS)  LOG_ERROR("Error in flush: %i\n", (int)error);

    // error = clFinish(sinoscope->opencl->queue);
    // if(error != CL_SUCCESS)  LOG_ERROR("Error in finish: %i\n", (int)error);

    error = clEnqueueReadBuffer(sinoscope->opencl->queue, sinoscope->opencl->buffer, CL_TRUE,
                                0, sinoscope->buffer_size, sinoscope->buffer, 0, NULL, NULL);
    if(error != CL_SUCCESS)  LOG_ERROR("Error in read buffer: %i\n", (int)error);

    return 0;

fail_exit:
    return -1;
}
