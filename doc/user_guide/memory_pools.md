# Memory Pools

## Pool

Hedgehog provides a NUMA-aware memory pool for preallocating objects. The pool
uses a lock-free Treiber stack internally (`IndexAllocator`), making `allocate`
and `release` safe to call concurrently from any thread without external
synchronization.

```cpp
hh::Pool<MatrixTile> pool(numa_id, device_id);

// preallocate 64 tiles, forwarding constructor arguments
pool.fill(64, TILE_WIDTH, TILE_HEIGHT);

MatrixTile *tile = pool.allocate();
// ... use tile ...
pool.release(tile);
```

### Constructor

```cpp
Pool();                                        // default (no NUMA, no device)
explicit Pool(int numa_id, int device_id = -1); // NUMA-aware, optional CUDA device
```

When a `device_id >= 0` is set, the pool calls `cudaSetDevice` before
constructing and destroying objects, so that any GPU allocations performed by
the constructor/destructor target the correct device.

### `fill(count, args...)`

Allocates `count` objects, constructing each in-place with `args...`. After
`fill`, the pool owns `count` ready-to-use objects. This must be called before
any `allocate` call.

### `allocate(wait)`

```cpp
T *allocate(bool wait = false);
```

- `wait = false` (default): returns `nullptr` immediately if the pool is empty.
- `wait = true`: blocks until another thread releases an object back to the
  pool (uses a counting semaphore internally).

### `release(ptr)`

Returns an object to the pool. If the object's type satisfies the
`HasCleanMemory` concept (i.e. provides a `clean_memory()` method), that method
is called automatically before the object becomes available for reuse:

```cpp
struct MatrixTile {
    float *data = nullptr;
    bool dirty = false;

    void clean_memory() {
        dirty = false;
        // reset tile state without deallocating
    }
};
```

### Usage in a task

```cpp
struct TileProducer {
    using inputs  = hh::type_list<TileRequest>;
    using outputs = hh::type_list<MatrixTile>;

    hh::Pool<MatrixTile> *pool_;

    TileProducer(hh::Pool<MatrixTile> *pool) : pool_(pool) {}

    void execute(auto ctx, hh::data_t<TileRequest> request) {
        MatrixTile *tile = pool_->allocate(/* wait = */ true);
        fill_tile(tile, request);
        ctx->push_result(tile);
    }
};
```

## AutomaticPool

`AutomaticPool` wraps `Pool` and returns `std::shared_ptr<T>` instead of raw
pointers. When the shared pointer's reference count drops to zero, the object
is automatically returned to the pool — no need to call `release`.

```cpp
hh::AutomaticPool<MatrixTile> pool;
pool.fill(64, TILE_WIDTH, TILE_HEIGHT);

std::shared_ptr<MatrixTile> tile = pool.allocate();
// tile is returned to the pool when the last shared_ptr goes out of scope
```

`AutomaticPool` also supports the `wait` flag:

```cpp
auto tile = pool.allocate(/* wait = */ true);
```

## MultiPool and MultiAutomaticPool

When a task handles multiple data types that need pooling, use `MultiPool` or
`MultiAutomaticPool`:

```cpp
hh::MultiPool<MatrixTile, VectorTile> pool(numa_id, device_id);

pool.fill<MatrixTile>(32, TILE_W, TILE_H);
pool.fill<VectorTile>(64, VEC_LEN);

MatrixTile *mt = pool.allocate<MatrixTile>();
VectorTile *vt = pool.allocate<VectorTile>();

pool.release(mt);
pool.release(vt);
```

## Pool vs AutomaticPool vs Arena

| Feature  | Pool                                    | AutomaticPool                 | Arena                           |
|----------|-----------------------------------------|-------------------------------|---------------------------------|
| Returns  | `T*`                                    | `shared_ptr<T>`               | `T*` (arena-managed)            |
| Release  | manual `release()`                      | automatic (ref count)         | arena destruction               |
| Reuse    | yes (recycled)                          | yes (recycled)                | no (bulk free)                  |
| Best for | high-perf recycling with manual control | shared ownership across nodes | short-lived scratch allocations |
