#ifndef FE7_RUNTIME_GBA_PTR_H_
#define FE7_RUNTIME_GBA_PTR_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fe7 {

struct GbaMemoryMap {
  uint8_t* ewram = nullptr;  // 0x02000000 (256 KiB)
  uint8_t* iwram = nullptr;  // 0x03000000 (32 KiB)
  uint8_t* rom = nullptr;    // 0x08000000 (16 MiB)
  size_t rom_size = 0;
};

extern GbaMemoryMap g_mem;

inline void* ResolveGbaRaw(uint32_t addr, size_t size, size_t align) {
  assert(addr != 0 && "Null GBA pointer dereference");
  assert((addr % align) == 0 && "Unaligned GBA pointer dereference");
  const uint32_t region = addr >> 24;
  const uint32_t offset = addr & 0x00FFFFFFu;
  switch (region) {
    case 0x02:
      assert(g_mem.ewram != nullptr && "EWRAM backing store not initialized");
      assert(offset + size <= 0x40000u && "EWRAM out of bounds");
      return g_mem.ewram + offset;
    case 0x03:
      assert(g_mem.iwram != nullptr && "IWRAM backing store not initialized");
      assert(offset + size <= 0x8000u && "IWRAM out of bounds");
      return g_mem.iwram + offset;
    case 0x08:
    case 0x09:
      assert(g_mem.rom != nullptr && "ROM backing store not initialized");
      assert((addr - 0x08000000u) + size <= g_mem.rom_size && "ROM out of bounds");
      return g_mem.rom + (addr - 0x08000000u);
    default:
      assert(false && "Use hal::Mmio* or hal::Vram* for IO/VRAM/OAM/PAL regions");
      return nullptr;
  }
}

template <typename T>
struct GbaPtr {
  static_assert(std::is_trivially_copyable_v<T>, "GbaPtr<T> requires trivially copyable POD");
  uint32_t addr = 0;

  constexpr GbaPtr() = default;
  constexpr explicit GbaPtr(uint32_t raw) : addr(raw) {}
  constexpr explicit operator bool() const { return addr != 0; }

  constexpr bool operator==(const GbaPtr& other) const = default;
  constexpr bool operator!=(const GbaPtr& other) const = default;

  T* get() const { return static_cast<T*>(ResolveGbaRaw(addr, sizeof(T), alignof(T))); }
  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }
  T& operator[](ptrdiff_t idx) const {
    const uint32_t elem_addr =
        static_cast<uint32_t>(static_cast<int64_t>(addr) + idx * static_cast<ptrdiff_t>(sizeof(T)));
    return *GbaPtr<T>(elem_addr).get();
  }
  GbaPtr<T> operator+(ptrdiff_t n) const {
    return GbaPtr<T>(
        static_cast<uint32_t>(static_cast<int64_t>(addr) + n * static_cast<ptrdiff_t>(sizeof(T))));
  }
};

template <typename T>
struct GbaSpan {
  GbaPtr<T> data;
  size_t count = 0;

  constexpr GbaSpan() = default;
  constexpr GbaSpan(GbaPtr<T> ptr, size_t n) : data(ptr), count(n) {}

  constexpr size_t size() const { return count; }
  constexpr bool empty() const { return count == 0; }

  T& operator[](size_t idx) const {
    assert(idx < count && "GbaSpan index out of bounds");
    return data[static_cast<ptrdiff_t>(idx)];
  }
};

// Explicit typed cast for FE7 Proc* subclasses (replaces unsafe C void* casts).
template <typename SubProc, typename BaseProc>
inline GbaPtr<SubProc> ProcAs(GbaPtr<BaseProc> p) {
  return GbaPtr<SubProc>(p.addr);
}

}  // namespace fe7

#endif  // FE7_RUNTIME_GBA_PTR_H_
