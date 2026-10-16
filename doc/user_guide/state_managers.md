# State Managers

State managers allow sharing a state between multiple nodes or multiple graphs.
The state follows the same API as the task (inputs, outputs, execute) but can
also implement `lock` and `unlock` methods. The state manager calls `lock`
before forwarding data to `execute` and `unlock` after, ensuring thread safety.

## State definition

`hh::MutexState` is a utility base class that provides mutex-based locking.

```cpp
struct AccumulatorState : hh::MutexState {
    using inputs  = hh::type_list<int>;
    using outputs = hh::type_list<int>;

    int    accumulator = 0;
    size_t counter     = 0;
    size_t ttl         = 0;

    AccumulatorState(size_t ttl) : ttl(ttl) {}

    void execute(auto ctx, hh::data_t<int> data) {
        accumulator += *data;
        ++counter;

        if (counter == ttl) {
            ctx->push_result(hh::make_data<int>(accumulator));
            accumulator = 0;
            counter     = 0;
        }
    }
};
```

## Node creation

```cpp
auto acc = std::make_shared<AccumulatorState>(10);

// share the same state across multiple state manager nodes
auto sm1 = hh::make_state_manager(acc, "Accumulator");
auto sm2 = hh::make_state_manager(acc, "Accumulator");
```

Both `sm1` and `sm2` hold a reference to the same `AccumulatorState` instance.
When data arrives at either node, the state is locked, `execute` is called, and
the state is unlocked.

## Important: single-threaded

A state manager always runs with `number_threads = 1`. Calling `copy()` on a
state manager is a fatal error — the state must be shared, not duplicated. If
you need parallelism, use multiple state manager nodes that reference the same
shared state.

## Locking

Locking is **optional**. The `Lockable` concept requires only two methods:

```cpp
void lock();
void unlock();
```

Any class providing these methods satisfies the concept. `hh::MutexState` is
the most common choice, but you can implement your own (e.g. using a
spinlock).

### Using atomics instead

When the state only contains atomic variables, locking is unnecessary. Simply
don't inherit from `MutexState` and don't provide `lock`/`unlock`:

```cpp
struct AtomicCounter {
    using inputs  = hh::type_list<int>;
    using outputs = hh::type_list<int>;

    std::atomic<int> count{0};
    int threshold;

    AtomicCounter(int threshold) : threshold(threshold) {}

    void execute(auto ctx, hh::data_t<int> data) {
        int prev = count.fetch_add(1, std::memory_order_relaxed);
        if (prev + 1 == threshold) {
            ctx->push_result(hh::make_data<int>(prev + 1));
        }
    }
};
```

The state manager will skip the `lock`/`unlock` calls if the state is not
`Lockable`.

## Remarks

- A state is most useful when the same data must be shared across nodes. When
  sharing is not needed, a single-threaded task node works the same way without
  the locking overhead.
- State managers are mainly used to share state across multiple graphs in
  [execution pipelines](execution_pipelines.md).
- The shared data doesn't need to live inside the state class: when making
  reusable sub-graphs, the shared state might be attached to the data itself.
