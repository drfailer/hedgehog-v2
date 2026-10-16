# Allocator

## Arena allocators background

An arena is a bump allocator that is used to pre-allocate chunks of memory
and do fast sub-allocations into it. It works the same way as a stack:
allocating memory moves the offset index (push), and one can do so until the
chunk is full. Only the last allocation can be resized. Resetting the arena
(free all) is as simple as resetting the offset back to 0 (no syscall, no global
allocator mutex).

Arenas represent a global lifetime for a group of object: instead of doing
scattered allocation on the heap, we group those allocations inside the arena,
which will allow to free all the items at once. It helps reducing the number of
calls to complex general purpose allocators such as `malloc`, keep the memory
contiguous (cache friendly) and facilitate memory management.

For example, the standard way to construct a binary tree is to allocate every
node separately. This gives no control on where nodes are located in memory
(causing potential cache disruption when iterating over the nodes) and makes
the tree deletion non trivial (recurse on every sub-tree, call free multiple
times which is very expensive). The cost of scattered allocations is much more
harmful than people think, so harmful that some compilers literally
remove some free calls when they know that the OS will free the memory anyway
(the OS free will be more efficient; they only do so when they can prove that
there will be no runtime leak which is difficult). A good example of this are
graphical applications than don't close immediately: this is often due to
inefficient widget deallocation. Arenas solve this problem by grouping the
allocations (free a single memory block instead of hundreds of widgets).

Both `Arena` and `DynamicArena` are move-only types (copy is deleted). This
prevents accidental aliasing of the underlying memory.

## Static Arena

The static arena as a fixed size: it cannot grow, once the chunk is full, it
will not reallocate any memory.

```cpp
hh::Arena arena(1024); // pre-allocate 1024 bytes

// standard alloc align inside the arena
std::byte *buf = arena.allocate(/* nb_bytes = */ 256, /* align = */ 8);

// allocate specific types
int *int_ptr = arena.allocate<int>();

// allocate multiple elements
int *int_array = arena.allocate<int>(10);

// helper functions
assert(arena.capacity() == 1024);
assert(arena.bytes_allocated() == 256 + 11 * sizeof(int));
assert(arena.bytes_available() == 1024 - arena.bytes_allocated());
assert(arena.contains(int_ptr));

// free all
arena.reset();
```

The `Arena` has multiple constructors:

```cpp
hh::Arena arena(1024);           // allocates 1024 bytes
hh::Arena arena(1024, numa_id);  // allocates 1024 bytes on numa node
hh::Arena arena(buf, buf_size);  // use a given buffer
hh::Arena arena(span);           // use a given span
```

If you use the buffer or span constructor, the arena will not own the memory
(it is up to you to free it). This is useful for two reasons:

1. You may give a static array as a buffer.
2. You may give a GPU buffer.

## Dynamic Arena

The dynamic arena manages a linked list of memory blocks. When the current
block is full, it will dynamically allocate a new blocks and do sub-allocations
into it. This arena also allows you to allocate bigger buffers than the
configured block size (often called out of band or oversized allocations which
are freed upon reset).

```cpp
hh::DynamicArena arena(/* block size = */ 1024);

// the API is the same as the static arena
std::byte *bytes = arena.allocate(2048, 8); // oversized allocation !

int *int_ptr   = arena.allocate<int>();
int *int_array = arena.allocate<int>(10);

// resize the last allocation (returns true on success, false if not possible)
bool ok = arena.resize(int_array, /* old_count = */ 10, /* new_count = */ 20);

// helper functions
arena.num_blocks();            // number of allocated blocks
arena.num_out_band();          // number of oversized allocations
arena.total_bytes_capacity();  // total capacity across all blocks

// free all
arena.reset();
```

The `DynamicArena` has multiple constructors:

```cpp
hh::DynamicArena arena(block_size);                                // default out-of-band threshold (1MB)
hh::DynamicArena arena(block_size, numa_id);                       // with NUMA node
hh::DynamicArena arena(block_size, out_band_threshold, numa_id);   // custom out-of-band threshold
```

Allocations larger than the out-of-band threshold go to a separate list and
are freed on `reset()`.

## Smart pointers

Hedgehog provides smart pointer functions to do arena allocations. The returned
pointers use custom deleters (`ArenaDeleter` / `DynamicArenaDeleter`) that call
the destructor but do not free memory (the arena owns it).

```cpp
// static arena
std::shared_ptr<int>                     sp = hh::make_shared<int>(arena);
std::unique_ptr<int, hh::ArenaDeleter>   up = hh::make_unique<int>(arena);

// dynamic arena
std::shared_ptr<int>                            sp = hh::make_shared<int>(dyn_arena);
std::unique_ptr<int, hh::DynamicArenaDeleter>   up = hh::make_unique<int>(dyn_arena);
```

There is also a `make_data` function which will allocate a `data_t`:

```cpp
data_t<int> data = hh::make_data<int>(arena);
```

In the default shared pointer mode, `make_data` returns a `shared_ptr`. When
`HH_POINTER_MODE` is defined, it releases the `unique_ptr` and returns a raw
pointer (matching `data_t<T> = T*`).
