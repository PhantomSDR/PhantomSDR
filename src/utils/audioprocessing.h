#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

#include <cstddef>
#include <deque>

class AGC {
  private:
    float desired_level;
    float attack_coeff;
    float release_coeff;
    size_t look_ahead_samples;
    float gain;
    std::deque<float> lookahead_buffer;
    std::deque<float> lookahead_max;
    float sample_rate;

    void push(float sample);
    void pop();
    float max();

  public:
    AGC(float desiredLevel = 0.1f, float attackTimeMs = 50.0f,
        float releaseTimeMs = 300.0f, float lookAheadTimeMs = 10.0f,
        float sr = 44100.0f);
    void process(float *arr, size_t len);
    void reset();
};

#endif