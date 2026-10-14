# Hedgehog User Reference

This reference guide uses the `optional` and `required` keywords which are not
part of the C++ language (treat them as empty macros).

# Task

```cpp
struct Task {
    required using inputs = hh::type_list<...>;
    required using outputs = hh::type_list<...>;
    optional using node_input = ...;
    optional using node_output = ...;

    optinal initialize(optional auto ctx) {...}
    optinal finalize(optional auto ctx) {...}

    // execute for each input type (can be member or static)
    required void execute(optinal auto ctx, data_t<...> data) {...}

    // may also use default constructor
    optinal std::make_shared<Task> copy() {...}
};
```

- Configuration:
  - `inputs`: list of input types.
  - `outputs`: list of output types.
  - `node_input`: type of the node input (handles the input queue
    and thread sleeping logic).
  - `node_output`: type of the node output (handles transmission to
    other tasks).
- Methods:
  - `initialize`: executed before the nodes start consuming data (each thread
    initialize its task copy).
  - `finalize`: executed before threads are joined (each thread finalize its
    task copy).
  - `execute`: process input data.
  - `copy`: copy the task (use default constructor if not specified; only used
    if the task has more than 1 thread).
