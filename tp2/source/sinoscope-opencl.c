#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include "log.h"
#include "sinoscope.h"

typedef struct sinoscope_opencl_args {
    float interval_inverse;
    float time;
    float max;
    float phase0;
    float phase1;
    float dx;
    float dy;

    unsigned int width;
    unsigned int height;
    unsigned int taylor;

    unsigned int interval;
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
        width * height * 3U, NULL, &error 
    );
    if (error != CL_SUCCESS) return -1;

    size_t size = 1111110;
    char* code = NULL;
    opencl_load_kernel_code(&code, &size);
    cl_program program = clCreateProgramWithSource(opencl->context, 1, (const char**) &code, &size, &error);
    if (error != CL_SUCCESS) return -1;
    
    error = clBuildProgram(program, 1, opencl->device_id, NULL, NULL, NULL);

    clGetProgramBuildInfo(program, opencl->device_id, CL_PROGRAM_BUILD_LOG)

    opencl->kernel = clCreateKernel(program, "sinoscope", &error);
    if (error != CL_SUCCESS) LOG_ERROR("Error creating kernel: %i\n", (int)error);

	return 0;
}

void sinoscope_opencl_cleanup(sinoscope_opencl_t* opencl)
{
    if(opencl->queue) clReleaseCommandQueue(opencl->queue);
    if(opencl->buffer) clReleaseMemObject(opencl->buffer);
    if(opencl->kernel) clReleaseKernel(opencl->kernel);
    if(opencl->context) clReleaseContext(opencl->context);
}


int sinoscope_image_opencl(sinoscope_t* sinoscope) {
    LOG_ERROR("ALLO\n");
    if (sinoscope == NULL) {
        LOG_ERROR_NULL_PTR();
        goto fail_exit;
    }
    
    cl_event event;
    cl_int error;

    const size_t work_dim = sinoscope->width * sinoscope->height * 3U; 

    error = clSetKernelArg(sinoscope->opencl->kernel, 0, sinoscope->buffer_size, &sinoscope->buffer);
    if(error != CL_SUCCESS) return -1;

    sinoscope_opencl_args_t args = {
        sinoscope->interval_inverse,
        sinoscope->time,
        sinoscope->max,
        sinoscope->phase0,
        sinoscope->phase1,
        sinoscope->dx,
        sinoscope->dy,
        sinoscope->width,
        sinoscope->height,
        sinoscope->taylor,
        sinoscope->interval
    };

    error = clSetKernelArg(sinoscope->opencl->kernel, 1, sizeof(args), &args);
    if(error != CL_SUCCESS) return -1;
    
    error = clEnqueueNDRangeKernel(sinoscope->opencl->queue, sinoscope->opencl->kernel, 2, 
                                    NULL, &work_dim, NULL, 0, NULL, &event);
    if(error != CL_SUCCESS)  return -1;

    error = clFinish(sinoscope->opencl->queue);
    if(error != CL_SUCCESS)  return -1;

    error = clEnqueueReadBuffer(sinoscope->opencl->queue, sinoscope->opencl->buffer, CL_TRUE,
                                0, sizeof(cl_mem), sinoscope->buffer, 0, NULL, &event);
    if(error != CL_SUCCESS)  return -1;

    return 0;

fail_exit:
    return -1;
}
