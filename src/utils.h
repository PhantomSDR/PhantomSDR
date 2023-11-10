#ifndef UTILS_H
#define UTILS_H

#include <cmath>
#include <complex>
#include <iostream>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>

#include <boost/circular_buffer.hpp>

std::string generate_unique_id();

template <typename T> class Neumaier {
  public:
    Neumaier(T init) : sum{init}, correction{0} {
        static_assert(std::is_floating_point<T>::value,
                      "Neumaier only works with floating point types");
    }
    Neumaier() : Neumaier(0) {}
    inline operator T() const { return sum; }
    inline Neumaier<T> &operator+=(T value) {
        T t = sum + value;
        if (std::abs(sum) >= std::abs(value)) {
            correction += (sum - t) + value;
        } else {
            correction += (value - t) + sum;
        }
        sum = t;
        return *this;
    }
    inline Neumaier<T> &operator-=(T value) { return *this += -value; }

  protected:
    T sum;
    T correction;
};

template <typename T> class Klein {
  public:
    Klein() : sum{0}, cs{0}, ccs{0}, c{0}, cc{0} {
        static_assert(std::is_floating_point<T>::value,
                      "Klein only works with floating point types");
    }
    inline operator T() const { return sum + cs + ccs; }
    inline Klein<T> &operator+=(T value) {
        T t = sum + value;
        if (std::abs(sum) >= std::abs(value)) {
            c = (sum - t) + value;
        } else {
            c = (value - t) + sum;
        }
        sum = t;
        t = cs + c;
        if (std::abs(cs) >= std::abs(c)) {
            cc = (cs - t) + c;
        } else {
            cc = (c - t) + cs;
        }
        cs = t;
        ccs += cc;
        return *this;
    }

  protected:
    T sum;
    T cs;
    T ccs;
    T c;
    T cc;
};

template <typename T> class MovingAverage {
  public:
    MovingAverage(){};
    MovingAverage(int length) : length{length}, num{0}, q{length, 0}, sum{0} {}
    inline T insert(T val) {
        sum -= q.back();
        q.push_front(val);
        sum += val;
        return getAverage();
    }
    inline T getAverage() { return sum / length; }
    inline void reset() {
        sum = 0;
        fill(q.begin(), q.end(), 0);
    }
    boost::circular_buffer<T> &getBuffer() { return q; }

  protected:
    int length;
    int num;
    boost::circular_buffer<T> q;
    std::conditional<std::is_floating_point<T>::value, Neumaier<T>, T>::type
        sum;
};

template <class T> class MovingMode {
  public:
    MovingMode(){};
    MovingMode(int length) : length{length}, q{length, 0} {}
    inline T insert(T val) {
        increment(q.back(), -1);
        q.push_front(val);
        increment(val, 1);
        return getMode();
    }
    inline void increment(T val, int amount) {
        v.erase({m[val], val});
        m[val] += amount;
        if (m[val] > 0) {
            v.insert({m[val], val});
        } else {
            m.erase(val);
        }
    }
    inline T getMode() {
        if (v.empty()) {
            return 0;
        }
        return v.begin()->second;
    }
    inline void reset() {
        m.clear();
        v.clear();
        fill(q.begin(), q.end(), 0);
    }

  protected:
    int length;
    boost::circular_buffer<T> q;
    std::set<std::pair<int, T>, std::greater<std::pair<int, T>>> v;
    std::unordered_map<T, int> m;
};

template <class T> class DCBlocker {
  public:
    DCBlocker() : DCBlocker(256) {}
    DCBlocker(int delay)
        : delay{delay}, last{0}, movingAverage1{delay}, movingAverage2{delay} {}
    void setAlpha(float alpha) { this->alpha = alpha; }
    inline T processSample(T s) {
        float ma1 = movingAverage1.insert(s);
        float ma2 = movingAverage2.insert(ma1);
        return movingAverage1.getBuffer()[delay - 1] - ma2;
    }
    void removeDC(T *arr, int length) {
        for (int i = 0; i < length; i++) {
            arr[i] = processSample(arr[i]);
            /*T temp = arr[i] + alpha * last;
            arr[i] = temp - last;
            last = temp;*/
        }
    }
    void reset() {
        movingAverage1.reset();
        movingAverage2.reset();
    }

  protected:
    int delay;
    T last;
    T alpha = 0.95;
    MovingAverage<T> movingAverage1;
    MovingAverage<T> movingAverage2;
};

#endif