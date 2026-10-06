// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the
// software in any medium, provided that you keep intact this entire notice. You may improve, modify and create
// derivative works of the software or any portion of the software, and you may copy and distribute such modifications
// or works. Modified works should carry a notice stating that you changed the software and should note the date and
// nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the
// source of the software. NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND,
// EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR
// WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE
// CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS
// THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE. You
// are solely responsible for determining the appropriateness of using and distributing the software and you assume
// all risks associated with its use, including but not limited to the risks and costs of program errors, compliance
// with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of
// operation. This software is not intended to be used in any situation where a failure could cause risk of injury or
// damage to property. The software developed by NIST employees is not subject to copyright protection within the
// United States.

#ifndef HEDGEHOG_IMPL_MEMORY_ALLOCATORS_H
#define HEDGEHOG_IMPL_MEMORY_ALLOCATORS_H

#include <memory>
#include <span>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <cassert>

#include "../../tool/numa_alloc.hpp"
#include "../../tool/data.hpp"

namespace hh {

// Arena Deleter ///////////////////////////////////////////////////////////////

struct ArenaDeleter {
    void *arena = nullptr;  // Opaque pointer - not used for deallocation

    template <typename T>
    void operator()(T *ptr) const noexcept {
        if (ptr) {
            ptr->~T();  // Explicit destructor, arena owns memory
        }
    }
};

// Arena ///////////////////////////////////////////////////////////////////////

//
// Fixed-size monotonic bump allocator. Manages raw bytes only - user is
// responsible for object construction/destruction via placement new or
// smart pointer helpers (make_shared/make_unique).
//
// Thread-local by design - no synchronization overhead.
// Supports NUMA-aware allocation like Pool<T>.
//

struct Arena {
    std::byte *buffer_ = nullptr;
    size_t capacity_bytes_ = 0;
    size_t offset_ = 0;
    bool owns_memory_ = false;
    int numa_id_ = -1;
    size_t alloc_size_ = 0;  // Actual allocated size for NUMA free

    // Constructors ////////////////////////////////////////////////////////////

    Arena() = default;

    explicit Arena(size_t capacity, int numa_id = -1)
        : capacity_bytes_(capacity), owns_memory_(true), numa_id_(numa_id) {

        if (numa_id_ >= 0) {
            buffer_ = static_cast<std::byte *>(
                numa::alloc_on_node(capacity_bytes_, numa_id_, alloc_size_));
            if (!buffer_) {
                // NUMA allocation failed, fallback
                numa_id_ = -1;
                buffer_ = static_cast<std::byte*>(std::aligned_alloc(64, capacity_bytes_));
            }
        } else {
            buffer_ = static_cast<std::byte*>(std::aligned_alloc(64, capacity_bytes_));
        }

        assert(buffer_ && "Arena: failed to allocate buffer");
    }

    Arena(std::byte *buffer, size_t size)
        : buffer_(buffer), capacity_bytes_(size), owns_memory_(false) {}

    Arena(std::span<std::byte> buffer)
        : buffer_(buffer.data()), capacity_bytes_(buffer.size()), owns_memory_(false) {}

    ~Arena() {
        if (!owns_memory_ || !buffer_) return;

        if (numa_id_ >= 0) {
            numa::free_on_node(buffer_, alloc_size_);
        } else {
            std::free(buffer_);
        }
    }

    // Move semantics //////////////////////////////////////////////////////////

    Arena(Arena const&) = delete;
    Arena& operator=(Arena const&) = delete;

    Arena(Arena &&other) noexcept
        : buffer_(other.buffer_)
        , capacity_bytes_(other.capacity_bytes_)
        , offset_(other.offset_)
        , owns_memory_(other.owns_memory_)
        , numa_id_(other.numa_id_)
        , alloc_size_(other.alloc_size_) {
        other.buffer_ = nullptr;
        other.capacity_bytes_ = 0;
        other.offset_ = 0;
        other.owns_memory_ = false;
        other.alloc_size_ = 0;
    }

    Arena& operator=(Arena &&other) noexcept {
        if (this == &other) return *this;

        // Free current buffer if owned
        if (owns_memory_ && buffer_) {
            if (numa_id_ >= 0) {
                numa::free_on_node(buffer_, alloc_size_);
            } else {
                std::free(buffer_);
            }
        }

        // Transfer from other
        buffer_ = other.buffer_;
        capacity_bytes_ = other.capacity_bytes_;
        offset_ = other.offset_;
        owns_memory_ = other.owns_memory_;
        numa_id_ = other.numa_id_;
        alloc_size_ = other.alloc_size_;

        other.buffer_ = nullptr;
        other.capacity_bytes_ = 0;
        other.offset_ = 0;
        other.owns_memory_ = false;
        other.alloc_size_ = 0;

        return *this;
    }

    // Allocation //////////////////////////////////////////////////////////////

    [[nodiscard]] std::byte* allocate(size_t bytes, size_t alignment) {
        // Use std::align to compute aligned pointer
        void* ptr = &buffer_[offset_];
        size_t space = capacity_bytes_ - offset_;

        void* aligned = std::align(alignment, bytes, ptr, space);
        if (!aligned) {
            return nullptr;  // Not enough space
        }

        // Update offset
        offset_ = capacity_bytes_ - space + bytes;
        return static_cast<std::byte*>(aligned);
    }

    template <typename T>
    [[nodiscard]] T* allocate(size_t count = 1) {
        auto *ptr = reinterpret_cast<T*>(allocate(count * sizeof(T), alignof(T)));
        if (ptr) { for (size_t i = 0; i < count; ++i) new (ptr + i) T(); }
        return ptr;
    }

    void deallocate([[maybe_unused]] void* p, [[maybe_unused]] size_t bytes) noexcept {
        // No-op: arena owns all memory until reset/destruction
    }

    template <typename T>
    void deallocate([[maybe_unused]] T* p, [[maybe_unused]] size_t count = 1) noexcept {
        // No-op
    }

    // Operations //////////////////////////////////////////////////////////////

    void reset() noexcept {
        offset_ = 0;
    }

    size_t bytes_allocated() const noexcept {
        return offset_;
    }

    size_t bytes_available() const noexcept {
        return capacity_bytes_ > offset_ ? capacity_bytes_ - offset_ : 0;
    }

    bool contains(void const* ptr) const noexcept {
        auto addr = reinterpret_cast<uintptr_t>(ptr);
        auto start = reinterpret_cast<uintptr_t>(buffer_);
        return addr >= start && addr < start + capacity_bytes_;
    }
};

// Dynamic Arena Deleter ///////////////////////////////////////////////////////

struct DynamicArena;  // Forward declaration

struct DynamicArenaDeleter {
    void *arena = nullptr;  // Points to DynamicArena

    template <typename T>
    void operator()(T *ptr) const noexcept;
};

// DynamicArena ////////////////////////////////////////////////////////////////

//
// Growing multi-block arena allocator. Manages raw bytes only.
// Thread-local by design (no mutex). Uses bump-pointer allocation per block.
// When current block is full, allocates a new one. Oversized allocations
// go to separate out-of-band list. Supports reset/free_all and resize.
//
// This is similar to Odin's dynamic arena:
// https://pkg.odin-lang.org/core/mem/#Dynamic_Arena
//

struct DynamicArena {
    struct Block {
        std::byte *data = nullptr;
        size_t capacity_bytes = 0;
        size_t alloc_size = 0;  // For NUMA free

        Block() = default;
        Block(std::byte *d, size_t cap, size_t alloc_sz = 0)
            : data(d), capacity_bytes(cap), alloc_size(alloc_sz) {}

        Block(Block &&other) noexcept
            : data(other.data)
            , capacity_bytes(other.capacity_bytes)
            , alloc_size(other.alloc_size) {
            other.data = nullptr;
            other.capacity_bytes = 0;
            other.alloc_size = 0;
        }

        Block& operator=(Block &&other) noexcept {
            if (this == &other) return *this;
            data = other.data;
            capacity_bytes = other.capacity_bytes;
            alloc_size = other.alloc_size;
            other.data = nullptr;
            other.capacity_bytes = 0;
            other.alloc_size = 0;
            return *this;
        }
    };

    size_t block_size_ = 4096;
    size_t out_band_size_ = 1024 * 1024;  // Threshold for oversized allocations
    int numa_id_ = -1;

    std::vector<Block> unused_blocks_;
    std::vector<Block> used_blocks_;
    std::vector<Block> out_band_allocations_;

    Block *current_block_ = nullptr;
    std::byte *current_pos_ = nullptr;
    size_t bytes_left_ = 0;

    // Constructors ////////////////////////////////////////////////////////////

    DynamicArena() = default;

    explicit DynamicArena(size_t block_size, int numa_id = -1)
        : block_size_(block_size), numa_id_(numa_id) {}

    DynamicArena(size_t block_size, size_t out_band_threshold, int numa_id = -1)
        : block_size_(block_size)
        , out_band_size_(out_band_threshold)
        , numa_id_(numa_id) {}

    ~DynamicArena() {
        free_all_blocks(unused_blocks_);
        free_all_blocks(used_blocks_);
        free_all_blocks(out_band_allocations_);
        if (current_block_) {
            free_block(*current_block_);
        }
    }

    // Move semantics //////////////////////////////////////////////////////////

    DynamicArena(DynamicArena const&) = delete;
    DynamicArena& operator=(DynamicArena const&) = delete;

    DynamicArena(DynamicArena &&other) noexcept
        : block_size_(other.block_size_)
        , out_band_size_(other.out_band_size_)
        , numa_id_(other.numa_id_)
        , unused_blocks_(std::move(other.unused_blocks_))
        , used_blocks_(std::move(other.used_blocks_))
        , out_band_allocations_(std::move(other.out_band_allocations_))
        , current_block_(other.current_block_)
        , current_pos_(other.current_pos_)
        , bytes_left_(other.bytes_left_) {
        other.current_block_ = nullptr;
        other.current_pos_ = nullptr;
        other.bytes_left_ = 0;
    }

    DynamicArena& operator=(DynamicArena &&other) noexcept {
        if (this == &other) return *this;

        // Free current state
        free_all_blocks(unused_blocks_);
        free_all_blocks(used_blocks_);
        free_all_blocks(out_band_allocations_);
        if (current_block_) {
            free_block(*current_block_);
        }

        // Transfer from other
        block_size_ = other.block_size_;
        out_band_size_ = other.out_band_size_;
        numa_id_ = other.numa_id_;
        unused_blocks_ = std::move(other.unused_blocks_);
        used_blocks_ = std::move(other.used_blocks_);
        out_band_allocations_ = std::move(other.out_band_allocations_);
        current_block_ = other.current_block_;
        current_pos_ = other.current_pos_;
        bytes_left_ = other.bytes_left_;

        other.current_block_ = nullptr;
        other.current_pos_ = nullptr;
        other.bytes_left_ = 0;

        return *this;
    }

    // Allocation //////////////////////////////////////////////////////////////

    [[nodiscard]] std::byte* allocate(size_t bytes, size_t alignment) {
        // Oversized allocation - use out-of-band list
        if (bytes > out_band_size_) {
            return allocate_out_band(bytes, alignment);
        }

        // Try current block
        if (current_block_ && bytes_left_ > 0) {
            void* ptr = current_pos_;
            size_t space = bytes_left_;

            void* aligned = std::align(alignment, bytes, ptr, space);
            if (aligned) {
                current_pos_ = static_cast<std::byte*>(aligned) + bytes;
                bytes_left_ = space - bytes;
                return static_cast<std::byte*>(aligned);
            }
        }

        // Current block exhausted, get new block
        get_or_create_block();

        // Allocate from new block
        void* ptr = current_pos_;
        size_t space = bytes_left_;

        void* aligned = std::align(alignment, bytes, ptr, space);
        if (!aligned) {
            return nullptr;  // Shouldn't happen with fresh block
        }

        current_pos_ = static_cast<std::byte*>(aligned) + bytes;
        bytes_left_ = space - bytes;
        return static_cast<std::byte*>(aligned);
    }

    template <typename T>
    [[nodiscard]] T* allocate(size_t count = 1) {
        auto *ptr = reinterpret_cast<T*>(allocate(count * sizeof(T), alignof(T)));
        if (ptr) { for (size_t i = 0; i < count; ++i) new (ptr + i) T(); }
        return ptr;
    }

    void deallocate([[maybe_unused]] void* p, [[maybe_unused]] size_t bytes) noexcept {
        // No-op: DynamicArena only supports bulk free via reset/free_all
    }

    template <typename T>
    void deallocate([[maybe_unused]] T* p, [[maybe_unused]] size_t count = 1) noexcept {
        // No-op
    }

    // Resize last allocation in current block (if possible)
    [[nodiscard]] bool resize(void *ptr, size_t old_size, size_t new_size) {
        if (!current_block_ || !ptr) return false;

        // Check if ptr is the last allocation in current block
        auto last_alloc = current_pos_ - old_size;
        if (ptr != last_alloc) return false;  // Not last allocation

        if (new_size <= old_size) {
            // Shrinking - reclaim space
            bytes_left_ += (old_size - new_size);
            current_pos_ = static_cast<std::byte*>(ptr) + new_size;
            return true;
        }

        // Growing - check if space available
        size_t additional = new_size - old_size;
        if (additional <= bytes_left_) {
            current_pos_ += additional;
            bytes_left_ -= additional;
            return true;
        }

        return false;  // Not enough space
    }

    // Operations //////////////////////////////////////////////////////////////

    void reset() noexcept {
        free_all();
    }

    void free_all() noexcept {
        // Move current block to unused
        if (current_block_) {
            unused_blocks_.push_back(std::move(*current_block_));
            delete current_block_;
            current_block_ = nullptr;
            current_pos_ = nullptr;
            bytes_left_ = 0;
        }

        // Move all used blocks to unused
        for (auto &block : used_blocks_) {
            unused_blocks_.push_back(std::move(block));
        }
        used_blocks_.clear();

        // Free all out-of-band allocations
        free_all_blocks(out_band_allocations_);
        out_band_allocations_.clear();
    }

    size_t num_blocks() const {
        size_t count = unused_blocks_.size() + used_blocks_.size();
        if (current_block_) ++count;
        return count;
    }

    size_t num_out_band() const {
        return out_band_allocations_.size();
    }

    size_t total_bytes_capacity() const {
        size_t total = 0;
        for (auto const &block : unused_blocks_) {
            total += block.capacity_bytes;
        }
        for (auto const &block : used_blocks_) {
            total += block.capacity_bytes;
        }
        if (current_block_) {
            total += current_block_->capacity_bytes;
        }
        for (auto const &block : out_band_allocations_) {
            total += block.capacity_bytes;
        }
        return total;
    }

private:
    void get_or_create_block() {
        // Move current block to used list
        if (current_block_) {
            used_blocks_.push_back(std::move(*current_block_));
        }

        // Try to reuse unused block
        if (!unused_blocks_.empty()) {
            current_block_ = new Block(std::move(unused_blocks_.back()));
            unused_blocks_.pop_back();
            current_pos_ = current_block_->data;
            bytes_left_ = current_block_->capacity_bytes;
            return;
        }

        // Allocate new block
        Block new_block = allocate_block(block_size_);
        current_block_ = new Block(std::move(new_block));
        current_pos_ = current_block_->data;
        bytes_left_ = current_block_->capacity_bytes;
    }

    std::byte* allocate_out_band(size_t bytes, size_t alignment) {
        // Allocate block with extra space for alignment
        size_t block_size = bytes + alignment - 1;
        Block new_block = allocate_block(block_size);

        // Align pointer using std::align
        void* ptr = new_block.data;
        size_t space = new_block.capacity_bytes;

        void* aligned = std::align(alignment, bytes, ptr, space);
        assert(aligned && "Out-of-band allocation failed to align");

        out_band_allocations_.push_back(std::move(new_block));
        return static_cast<std::byte*>(aligned);
    }

    Block allocate_block(size_t size) {
        std::byte *data = nullptr;
        size_t alloc_size = 0;

        if (numa_id_ >= 0) {
            data = static_cast<std::byte*>(
                numa::alloc_on_node(size, numa_id_, alloc_size));
            if (!data) {
                data = static_cast<std::byte*>(std::aligned_alloc(64, size));
            }
        } else {
            data = static_cast<std::byte*>(std::aligned_alloc(64, size));
        }

        assert(data && "DynamicArena: failed to allocate block");
        return Block(data, size, alloc_size);
    }

    void free_block(Block &block) {
        if (!block.data) return;

        if (numa_id_ >= 0 && block.alloc_size > 0) {
            numa::free_on_node(block.data, block.alloc_size);
        } else {
            std::free(block.data);
        }
        block.data = nullptr;
    }

    void free_all_blocks(std::vector<Block> &blocks) {
        for (auto &block : blocks) {
            free_block(block);
        }
    }
};

// DynamicArenaDeleter implementation (after DynamicArena definition) //////////

template <typename T>
void DynamicArenaDeleter::operator()(T *ptr) const noexcept {
    if (ptr) {
        ptr->~T();
        // No deallocation - DynamicArena only supports bulk free via reset()
    }
}

// Smart Pointer Helpers ///////////////////////////////////////////////////////

template <typename T, typename... Args>
std::shared_ptr<T> make_shared(Arena& arena, Args&&... args) {
    T* ptr = arena.allocate<T>(1);
    if (!ptr) return nullptr;

    new (ptr) T(std::forward<Args>(args)...);
    return std::shared_ptr<T>(ptr, ArenaDeleter{&arena});
}

template <typename T, typename... Args>
std::unique_ptr<T, ArenaDeleter> make_unique(Arena& arena, Args&&... args) {
    T* ptr = arena.allocate<T>(1);
    if (!ptr) return {nullptr, ArenaDeleter{&arena}};

    new (ptr) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, ArenaDeleter>(ptr, ArenaDeleter{&arena});
}

template <typename T, typename... Args>
std::shared_ptr<T> make_shared(DynamicArena& arena, Args&&... args) {
    T* ptr = arena.allocate<T>(1);
    if (!ptr) return nullptr;

    new (ptr) T(std::forward<Args>(args)...);
    return std::shared_ptr<T>(ptr, DynamicArenaDeleter{&arena});
}

template <typename T, typename... Args>
std::unique_ptr<T, DynamicArenaDeleter> make_unique(DynamicArena& arena, Args&&... args) {
    T* ptr = arena.allocate<T>(1);
    if (!ptr) return {nullptr, DynamicArenaDeleter{&arena}};

    new (ptr) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, DynamicArenaDeleter>(ptr, DynamicArenaDeleter{&arena});
}

// data_t Integration //////////////////////////////////////////////////////////

//
// Overloads of make_data that work with Arena/DynamicArena.
// These adapt to the current data mode (HH_POINTER_MODE or shared_ptr mode).
//

#ifdef HH_POINTER_MODE

template <typename T, typename... Args>
data_t<T> make_data(Arena &arena, Args&&... args) {
    return make_unique(arena, std::forward<Args>(args)...).release();
}

template <typename T, typename... Args>
data_t<T> make_data(DynamicArena &arena, Args&&... args) {
    return make_unique(arena, std::forward<Args>(args)...).release();
}

#else  // Shared ptr mode (default)

template <typename T, typename... Args>
data_t<T> make_data(Arena &arena, Args&&... args) {
    return make_shared<T>(arena, std::forward<Args>(args)...);
}

template <typename T, typename... Args>
data_t<T> make_data(DynamicArena &arena, Args&&... args) {
    return make_shared<T>(arena, std::forward<Args>(args)...);
}

#endif

} // end namespace hh

#endif
