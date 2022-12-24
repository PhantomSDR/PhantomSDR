#ifndef UTILS_H
#define UTILS_H

#include <cmath>
#include <iostream>
#include <queue>
#include <string>

void build_hann_window(float *arr, int num);
void build_blackman_harris_window(float *arr, int num);

std::string generate_unique_id();

template <class T> class MovingAverage {
  public:
    MovingAverage() : MovingAverage(0) {}
    MovingAverage(int length) : length{length}, sum{0} {}
    void setLength(int length) { this->length = length; }
    void insert(T val) {
        while ((int)q.size() > length - 1) {
            remove();
        }
        sum += val;
        q.emplace(val);
    }
    T remove() {
        T temp = q.front();
        sum -= temp;
        q.pop();
        return temp;
    }
    T getAverage() { return sum / q.size(); }

  protected:
    int length;
    std::queue<T> q;
    T sum;
};

template <class T> class DCBlocker {
  public:
    DCBlocker() : DCBlocker(0.999) {}
    DCBlocker(float alpha) : alpha{alpha}, prev{0}, value{0} {}
    void setAlpha(float alpha) { this->alpha = alpha; }
    void removeDC(T *arr, int length) {
        for (int i = 0; i < length; i++) {
            value = arr[i] - prev + alpha * value;
            prev = arr[i];
            arr[i] = value;
        }
    }
    void reset() {
        prev = 0;
        value = 0;
    }

  protected:
    float alpha;
    float prev;
    float value;
};

template <class T> class AGC {
  public:
    AGC() : AGC(0.9999, 0.1) {}
    AGC(float alpha, float target) : alpha{alpha}, target{target}, value{0} {}
    void setAlpha(float alpha) { this->alpha = alpha; }
    void setTarget(float target) { this->target = target; }
    void applyAGC(T *arr, int length) {
        for (int i = 0; i < length; i++) {
            value = alpha * value + (1 - alpha) * arr[i] * arr[i];
            arr[i] *= target / std::sqrt(value);
            arr[i] = std::max(std::min(arr[i], 1.0f), -1.0f);
        }
    }
    void reset() { value = 0; }

  protected:
    float alpha;
    float target;
    float value;
};
#endif