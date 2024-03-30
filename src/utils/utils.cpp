#include "utils.h"

#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

static boost::uuids::random_generator uuid_generator =
    boost::uuids::random_generator();

std::string generate_unique_id() {
    return boost::uuids::to_string(uuid_generator());
}


/*template <typename T> class MovingAverage {
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
};*/
