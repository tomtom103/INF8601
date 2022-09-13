#include <stdio.h>
#include "tbb/pipeline.h"

extern "C" {
#include "filter.h"
#include "pipeline.h"
}

class TBBLoadNext {
    image_dir_t* image_dir;
public:
    TBBLoadNext(image_dir_t* image_dir) : image_dir(image_dir) {}

    image_t* operator()(tbb::flow_control& fc) const {
        image_t* out = image_dir_load_next(image_dir);
        if (out == NULL) {
            fc.stop();
        }
        return out;
    }
};

class TBBScaleUp {
public:
    TBBScaleUp() {}
    image_t* operator()(image_t* in) const {
        image_t* out = filter_scale_up(in, 2);
        if (out == NULL) {
            exit(-1);
        }
        image_destroy(in);
        return out;
    }
};

class TBBSharpen {
public:
    TBBSharpen() {}
    image_t* operator()(image_t* in) const {
        image_t* out = filter_sharpen(in);
        if (out == NULL) {
            exit(-1);
        }
        image_destroy(in);
        return out;
    }
};

class TBBSobel {
public:
    TBBSobel() {}
    image_t* operator()(image_t* in) const {
        image_t* out = filter_sobel(in);
        image_destroy(in);
        return out;
    }
};

class TBBSave {
    image_dir_t* image_dir;
public:
    TBBSave(image_dir_t* image_dir) : image_dir(image_dir) {}

    void operator()(image_t* in) const {
        image_dir_save(image_dir, in);
        image_destroy(in);
    }
};

int pipeline_tbb(image_dir_t* image_dir) {
    tbb::parallel_pipeline(
        16,
        tbb::make_filter<void, image_t*>(tbb::filter::serial, TBBLoadNext(image_dir)) &
        tbb::make_filter<image_t*, image_t*>(tbb::filter::parallel, TBBScaleUp()) &
        tbb::make_filter<image_t*, image_t*>(tbb::filter::parallel, TBBSharpen()) &
        tbb::make_filter<image_t*, image_t*>(tbb::filter::parallel, TBBSobel()) &
        tbb::make_filter<image_t*, void>(tbb::filter::serial, TBBSave(image_dir))
    );
    return 0;
}