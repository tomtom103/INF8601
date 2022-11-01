#include "helpers.cl"

typedef struct __attribute__((packed)) sinoscope_opencl_args {
    float interval_inverse;
    float time;
    float max;
    float phase0;
    float phase1;
    float dx;
    float dy;
} sinoscope_opencl_args_t;

__kernel void sinoscope_kernel(
    __global unsigned char* buffer, 
    sinoscope_opencl_args_t sinoscope, 
    unsigned int width,
    unsigned int height,
    unsigned int taylor,
    unsigned int interval) {
    
    // Une seule dimension
    const int id = get_global_id(0);

    int i = id % height;
    int j = id / height; 
    
    float px    = sinoscope.dx * j - 2 * M_PI;
    float py    = sinoscope.dy * i - 2 * M_PI;
    float value = 0;

    for (int k = 1; k <= taylor; k += 2) {
        value += sin(px * k * sinoscope.phase1 + sinoscope.time) / k;
        value += cos(py * k * sinoscope.phase0) / k;
    }

    value = (atan(value) - atan(-value)) / M_PI;
    value = (value + 1) * 100;

    pixel_t pixel;
    color_value(&pixel, value, interval, sinoscope.interval_inverse);

    int index = (i * 3) + (j * 3) * width;

    buffer[index + 0] = pixel.bytes[0];
    buffer[index + 1] = pixel.bytes[1];
    buffer[index + 2] = pixel.bytes[2];
}