# Memory Pools

## Pool

Hedgehog provides a numa aware memory pool that can be used to preallocate
pointers. The pool will never allocate memory on the GPU, however, it will call
`cudaSetDevice` (using the configured device) before constructing and destroying
the allocated objects.

```cpp
hh::Pool<MatrixTile> pool(numa_id, device_id);

// the first argument is the number of allocated element, the rest is the
// arguments to pass to the item constructor
pool.fill(TILE_COUNT, TILE_WIDTH, TILE_HEIGHT);

MatrixTile *tile = pool.allocate();
pool.release(tile);
```

The `allocate` function takes a wait flag as argument (if the pool is empty,
the pool will block until another threads release memory):

```cpp
hh::Pool<MatrixTile> pool; // numa_id and device_id are optional

MatrixTile *tile = pool.allocate(/* wait = */ true);
```

## Automatic Pool

The `AutomaticPool` is a `Pool` wrapper that wraps the allocated pointers into
a shared pointer. Once the reference counter is 0, the shared pointer will
return the memory to the pool automatically (no need to call `release`).

```cpp
hh::AutomaticPool<MatrixTile> pool;

std::shared_ptr<MatrixTile> tile = pool.allocate();
```
