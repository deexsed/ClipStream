#pragma once
#include <stdexcept>
namespace clipstream::utils {
class PermissionDenied : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
class NotFound : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
} // namespace clipstream::utils
