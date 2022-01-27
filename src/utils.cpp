#include "utils.h"

#include <cmath>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

void build_hann_window(float *arr, int num) {
    // Use a Hann window
    for (int i = 0; i < num; i++) {
        arr[i] = 0.5 * (1 - cosf(2 * M_PI * i / num));
    }
}

void build_blackman_harris_window(float *arr, int num) {
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    for (int i = 0; i < num; i++) {
        arr[i] = a0 - (a1 * cosf((2.0f * M_PI * i) / (num - 1))) +
                 (a2 * cosf((4.0f * M_PI * i) / (num - 1))) -
                 (a3 * cosf((6.0f * M_PI * i) / (num - 1)));
    }
}

static boost::uuids::random_generator uuid_generator =
    boost::uuids::random_generator();

std::string generate_unique_id() {
    return boost::uuids::to_string(uuid_generator());
}