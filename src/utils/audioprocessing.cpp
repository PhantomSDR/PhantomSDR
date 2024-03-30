#include "audioprocessing.h"

#include <cmath>

AGC::AGC(float desiredLevel,  // Target level to normalize audio (-1 to 1 range)
         float attackTimeMs,  // Fast reaction to rising levels
         float releaseTimeMs, // Slower fallback to avoid pumping effect
         float lookAheadTimeMs, // Look-ahead for peak detection
         float sr)              // Sample rate
    : desired_level(desiredLevel), gain(0), sample_rate(sr) {
    look_ahead_samples =
        static_cast<size_t>(lookAheadTimeMs * sample_rate / 1000.0f);
    attack_coeff = 1 - exp(-1.0f / (attackTimeMs * 0.001f * sample_rate));
    release_coeff = 1 - exp(-1.0f / (releaseTimeMs * 0.001f * sample_rate));
}

void AGC::push(float sample) {
    lookahead_buffer.push_back(sample);
    while (lookahead_max.size() &&
           std::abs(lookahead_max.back()) < std::abs(sample)) {
        lookahead_max.pop_back();
    }
    lookahead_max.push_back(sample);

    if (lookahead_buffer.size() > look_ahead_samples) {
        this->pop();
    }
}

void AGC::pop() {
    float sample = lookahead_buffer.front();
    lookahead_buffer.pop_front();
    if (sample == lookahead_max.front()) {
        lookahead_max.pop_front();
    }
}

float AGC::max() { return std::abs(lookahead_max.front()); }

void AGC::process(float *arr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Add current sample to the lookahead buffer
        this->push(arr[i]);

        // If we have enough samples for the look-ahead, process the gain
        if (lookahead_buffer.size() == look_ahead_samples) {
            float current_sample = lookahead_buffer.front();

            // Calculate the desired gain
            float peak_sample = this->max();

            float desired_gain = desired_level / (peak_sample + 1e-15f);

            // Apply the attack/release smoothing
            if (desired_gain < gain) {
                gain = 
                    gain - attack_coeff * (gain - desired_gain);
            } else {
                gain = 
                    gain + release_coeff * (desired_gain - gain);
            }
            // Apply the gain to the current sample
            arr[i] = current_sample * gain;
        } else {
            arr[i] = 0.0f;
        }
    }
}

void AGC::reset() {
    gain = 0;
    lookahead_buffer.clear();
    lookahead_max.clear();
}