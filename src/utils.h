#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <queue>

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

template <class T> class AGC {
  public:
    AGC() : AGC(1) {}
    AGC(float alpha) : alpha{alpha}, value{0} {}
    void setAlpha(float alpha) { this->alpha = alpha; }
    void update(T val) {
      if (val > 10 * value) {
        value = val;
      } else {
        value = alpha * val + (1 - alpha) * value;
      }
    }
    T getValue() {
      return value;
    }
  protected:
    float alpha;
    float value;
};
#endif