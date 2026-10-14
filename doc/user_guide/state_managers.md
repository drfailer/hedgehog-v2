# State Managers

State managers allow to share a state between multiple nodes or multiple
graphs. The state follows the same API as the task but can also implement
`lock` and `unlock` methods that are called by the state managers before
processing data (before `execute` is called).

`hh::MutexState` is a utility class that can be used to create a mutex based
state.

```cpp
struct AccumulatorState : hh::MutexState {
    using inputs  = hh::type_list<int>;
    using outputs = hh::type_list<int>;

    // data protected under a mutex
    int    accumulator = 0;
    size_t counter     = 0;
    size_t ttl         = 0;

    State(size_t ttl) : ttl(ttl) {}

    void execute(auto ctx, hh::data_t<int> data) {
        // this is safe because the state is locked
        accumulator += *data;
        ++counter;

        // when the accumulation is done, we push the result and reset
        if (counter == ttl) {
            ctx->push_result(hh::make_data<int>(accumulator));
            accumulator = 0;
            counter     = 0;
        }
    }
};
```

Node creation:

```cpp
// create the state
auto acc = std::make_shared<Accumulator>(10);

// share the state to multiple state managers nodes
auto sm1 = hh::make_state_manager(acc, "Accumulator");
auto sm2 = hh::make_state_manager(acc, "Accumulator");
```

## Remarks

- A state is truely useful when the same data needs to be shared accross nodes.
  When it is not the case, a single threaded task node will work the same way
  (we don't need a lock in this case). State managers are mainly used to share
  the same state across multiple graphs in [execution pipelines](execution_pipelines.md).
- The `lock` and `unlock` functions are optional since one may use atomics.
- The shared data doesn't need to leave inside the state class: when making
  reusable sub-graphs, the shared state might be attached to the data.
