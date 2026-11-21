// Typed virtual-bus pointer and span wrappers for GBA memory regions.
//
// GBA game structures embed 32-bit GBA bus addresses rather than 64-bit host pointers. GbaPtr<T>
// preserves exact 32-bit struct layouts while resolving EWRAM (0x02xxxxxx), IWRAM (0x03xxxxxx),
// and ROM (0x08xxxxxx/0x09xxxxxx) addresses into host pointers with null, alignment, and bounds
// assertions in debug builds. IO/VRAM/OAM/PAL regions are intentionally disallowed here so callers
// go through the explicit hal::Mmio* / hal::Vram* APIs.

#ifndef FE7_RUNTIME_GBA_PTR_H_
#define FE7_RUNTIME_GBA_PTR_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fe7 {

// Host backing-store pointers for directly addressable GBA memory regions.
// Populated by hal::Init() (or test fixtures) and cleared by hal::Shutdown().
struct GbaMemoryMap {
  uint8_t* ewram = nullptr;  // 0x02000000..0x0203FFFF (256 KiB external WRAM)
  uint8_t* iwram = nullptr;  // 0x03000000..0x03007FFF (32 KiB internal WRAM)
  uint8_t* rom = nullptr;    // 0x08000000..0x09FFFFFF (up to 32 MiB Game Pak ROM)
  size_t rom_size = 0;       // Actual loaded ROM byte length for bounds checking
};

// Global active GBA memory map used by ResolveGbaRaw() and GbaPtr<T>.
extern GbaMemoryMap g_mem;

// Translates a 32-bit GBA bus address into a host pointer for an object of `size` bytes and
// `align`-byte alignment. Asserts on null, misaligned, out-of-bounds, or non-RAM/ROM regions.
inline void* ResolveGbaRaw(uint32_t addr, size_t size, size_t align) {
  assert(addr != 0 && "Null GBA pointer dereference");
  // Catch misaligned accesses early so host UB on typed dereferences mirrors GBA alignment rules.
  assert((addr % align) == 0 && "Unaligned GBA pointer dereference");
  // Top byte selects the GBA memory region; lower 24 bits hold the offset within that region.
  const uint32_t region = addr >> 24;
  const uint32_t offset = addr & 0x00FFFFFFu;
  switch (region) {
    case 0x02:
      assert(g_mem.ewram != nullptr && "EWRAM backing store not initialized");
      // EWRAM is strictly 256 KiB (0x40000 bytes); reject accesses that straddle the end.
      assert(offset + size <= 0x40000u && "EWRAM out of bounds");
      return g_mem.ewram + offset;
    case 0x03:
      assert(g_mem.iwram != nullptr && "IWRAM backing store not initialized");
      // IWRAM is strictly 32 KiB (0x8000 bytes); reject accesses that straddle the end.
      assert(offset + size <= 0x8000u && "IWRAM out of bounds");
      return g_mem.iwram + offset;
    case 0x08:
    case 0x09:
      assert(g_mem.rom != nullptr && "ROM backing store not initialized");
      // Wait State 0 spans 0x08000000..0x09FFFFFF (32 MiB), so subtract the base instead of using
      // the 24-bit offset so addresses in 0x09xxxxxx compute the right offset past 16 MiB.
      assert((addr - 0x08000000u) + size <= g_mem.rom_size && "ROM out of bounds");
      return g_mem.rom + (addr - 0x08000000u);
    default:
      // Hardware registers and graphics RAM have side effects or bus-width rules; force HAL usage.
      assert(false && "Use hal::Mmio* or hal::Vram* for IO/VRAM/OAM/PAL regions");
      return nullptr;
  }
}

// 32-bit typed GBA pointer wrapper that resolves into host memory on dereference.
// Layout is a single uint32_t so it can be embedded directly inside decompiled GBA structs.
template <typename T>
struct GbaPtr {
  // Backing memory is shared raw bytes with the fallback CPU, so non-trivial C++ types are unsafe.
  static_assert(std::is_trivially_copyable_v<T>, "GbaPtr<T> requires trivially copyable POD");
  uint32_t addr = 0;

  constexpr GbaPtr() = default;
  constexpr explicit GbaPtr(uint32_t raw) : addr(raw) {}

  // Evaluates to true when non-null (addr != 0).
  constexpr explicit operator bool() const { return addr != 0; }

  constexpr bool operator==(const GbaPtr& other) const = default;
  constexpr bool operator!=(const GbaPtr& other) const = default;

  // Resolves this GBA address to a typed host pointer (asserts on invalid/out-of-bounds address).
  T* get() const { return static_cast<T*>(ResolveGbaRaw(addr, sizeof(T), alignof(T))); }
  T* operator->() const { return get(); }
  T& operator*() const { return *get(); }

  // Indexes element `idx` relative to this pointer, validating alignment and bounds of the target.
  T& operator[](ptrdiff_t idx) const {
    // Perform arithmetic in 64-bit signed space so negative indices adjust the 32-bit address
    // cleanly without signed/unsigned wraparound surprises.
    const uint32_t elem_addr =
        static_cast<uint32_t>(static_cast<int64_t>(addr) + idx * static_cast<ptrdiff_t>(sizeof(T)));
    return *GbaPtr<T>(elem_addr).get();
  }

  // Advances this pointer by `n` elements of type T in GBA address space.
  GbaPtr<T> operator+(ptrdiff_t n) const {
    return GbaPtr<T>(
        static_cast<uint32_t>(static_cast<int64_t>(addr) + n * static_cast<ptrdiff_t>(sizeof(T))));
  }
};

// Non-owning bounds-checked view over a contiguous array of `count` elements in GBA memory.
template <typename T>
struct GbaSpan {
  GbaPtr<T> data;
  size_t count = 0;

  constexpr GbaSpan() = default;
  constexpr GbaSpan(GbaPtr<T> ptr, size_t n) : data(ptr), count(n) {}

  constexpr size_t size() const { return count; }
  constexpr bool empty() const { return count == 0; }

  // Returns a reference to element `idx`, asserting `idx < count` before resolving into host RAM.
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
