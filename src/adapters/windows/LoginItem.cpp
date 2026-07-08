/// @file LoginItem.cpp
/// Windows stub — login item / Run key support lands in Phase 6.

#include "core/LoginItem.hpp"

namespace focusgaze {

bool setOpenAtLogin(bool /*enabled*/, std::string* error_message) {
  if (error_message) *error_message = "open at login is not implemented on Windows yet";
  return false;
}

bool isOpenAtLoginEnabled() { return false; }

} // namespace focusgaze
