#include "utils.hh"

const uint8_t* byte_t::constBytes() const {
  return reinterpret_cast<uint8_t*>(
    const_cast<byte_t*>(this));
}

size_t byte_t::size() const {
  byte_impl_t* byte = static_cast<byte_impl_t*>(
    static_cast<void*>(
      reinterpret_cast<uint8_t*>(
        const_cast<byte_t*>(this)) - OFFSET_OF(byte_impl_t, data)));
  return byte->capacity;
}

void byte_t::acquire() {
  byte_impl_t* byte = static_cast<byte_impl_t*>(
    static_cast<void*>(
      reinterpret_cast<uint8_t*>(
        const_cast<byte_t*>(this)) - OFFSET_OF(byte_impl_t, data)));
  ++byte->refcount;
}

void byte_t::release() {
  byte_impl_t* byte = static_cast<byte_impl_t*>(
    static_cast<void*>(
      reinterpret_cast<uint8_t*>(
        const_cast<byte_t*>(this)) - OFFSET_OF(byte_impl_t, data)));
  if (--byte->refcount == 0) {
    free(byte);
  }
}

byte_t* byte_t::create(uint8_t* p,
                       size_t size) {
  byte_impl_t* byte = new(size) byte_impl_t(p, size);
  return byte->data;
}

byte_t* byte_t::create(size_t size) {
  byte_impl_t* byte = new(size) byte_impl_t(size);
  return byte->data;
}
