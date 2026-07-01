// Daisy stub for plaits/user_data.h
//
// The original implementation writes/reads custom wavetable data to STM32F37x
// internal flash using stm32f37x_conf.h — hardware that does not exist on Daisy.
// We don't use this feature; ptr() returning nullptr tells Plaits engines to use
// their built-in wavetables, which is the normal behaviour.

#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include <cstdint>
#include <cstddef>

namespace plaits {

class UserData {
 public:
  UserData() { }
  ~UserData() { }

  inline const uint8_t* ptr(int slot) const { return nullptr; }
  inline bool Save(uint8_t* /* rx_buffer */, int /* slot */) { return false; }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_
