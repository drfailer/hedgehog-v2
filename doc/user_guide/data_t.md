# The `data_t` type

When creating tasks, we often use `data_t`, which is a wrapper that allows
keeping similar APIs for different data modes. There are 3 data modes:

1. **Shared pointer mode** (default, similar to Hedgehog v1): data flowing
   through the graph is allocated using `std::shared_ptr`. This makes memory
   management easy when prototyping.
2. **Pointer mode**: data flowing through the graph is a raw pointer (`T*`).
   This forces users to manage memory manually (we have allocators for that),
   but avoids atomic overhead from shared pointers.
3. **Value mode**: gives full control over the type. Data is stored by value
   inside a `data_t<T>` wrapper. There is no compatibility with the other
   modes (you have to change the type list).

The default mode is the shared pointer mode because we noticed that on most
graphs, the atomic overhead was negligible when tasks do sufficient work. We
also make sure that there are very few shared pointer copies under the hood
(a shared pointer is copied only when it is sent to multiple nodes).

If you think that shared pointers cause performance issues (or you simply don't
like them), you can use the other modes:

- define `HH_POINTER_MODE` to use the pointer mode.
- define `HH_VALUE_MODE` to use the value mode.


## Creating data

Use `hh::make_data<T>(args...)` to create data in any mode. It forwards
constructor arguments and returns the appropriate `data_t<T>`:

```cpp
// shared_ptr mode: returns std::shared_ptr<int>
auto data = hh::make_data<int>(42);

// pointer mode: returns int* (allocated with new)
auto data = hh::make_data<int>(42);

// value mode: returns data_t<int> (wraps the value)
auto data = hh::make_data<int>(42);
```

Using `make_data` keeps your code portable across all three modes.


## Accessing data

The accessor semantics depend on the mode:

| Mode           | `data_t<T>` is       | `*data`                    | `data->member`                |
|----------------|----------------------|----------------------------|-------------------------------|
| Shared pointer | `std::shared_ptr<T>` | `T&` (via shared_ptr)      | `T*` (via shared_ptr)         |
| Pointer        | `T*`                 | `T&` (dereference)         | `T*` (arrow)                  |
| Value          | wrapper struct       | `T&` (returns inner value) | `T*` (pointer to inner value) |

In all three modes, `*data` gives a `T&` and `data->member` works as expected,
so task code using these operators is portable across modes.


## Extracting the underlying type

The `hh::data_type<D>` utility extracts the underlying `T` from a `data_t<T>`:

```cpp
using D = hh::data_t<int>;
using T = hh::data_type<D>; // T = int
```

This is useful in generic/template code that needs to recover the element type.


## Data movement

When data is transferred to 1 receiver node, it is **moved**. When it is
transferred to N receiver nodes, it is **copied** (N - 1) times and moved
once (the last receiver gets the move).

```
          ┌─ Node B  (copy)
data ─────┼─ Node C  (copy)
          └─ Node D  (move)
```

This means that if a task produces data that goes to a single downstream node,
no copies are made. When broadcasting to multiple nodes, the framework
minimizes copies by moving to the last receiver.
