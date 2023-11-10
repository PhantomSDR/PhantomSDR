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