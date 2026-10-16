# Logging

Hedgehog provides a thread-safe logging system in `hh::log`. All log functions
automatically capture source location (file, line, column).

## Log levels

```cpp
hh::log::LogLevel::Info     // inactive by default
hh::log::LogLevel::Warning  // default level
hh::log::LogLevel::Error    // prints + increments error count
```

Set the log level with:

```cpp
hh::log::set_level(hh::log::LogLevel::Info);
```

Messages below the current level are silently dropped.

## Usage

```cpp
#include <hedgehog/tool/log.hpp>

hh::log::info("processing batch ", batch_id);
hh::log::warning("queue size exceeded threshold: ", size);
hh::log::error("failed to allocate ", n, " bytes");
hh::log::fatal("invariant violated");  // calls std::abort
```

Each function accepts variadic arguments that are printed in sequence
(fold expression over `operator<<`).

- `info` writes to `std::cout`.
- `warning`, `error`, and `fatal` write to `std::cerr`.

Output format:

```
HH  INFO  file.cpp(42:5): processing batch 7
HH  WARN  file.cpp(50:5): queue size exceeded threshold: 1024
HH  ERROR file.cpp(58:5): failed to allocate 4096 bytes
HH  FATAL file.cpp(66:5): invariant violated
```

## Error counting

`error()` increments a global counter. Query it with:

```cpp
size_t count = hh::log::error_count();
```

This can be used for soft error tracking without aborting — accumulate errors
during a run and check the count at the end.

## Fatal

`fatal()` logs the message and calls `std::abort()`. It is marked `[[noreturn]]`.
Use it for invariant violations that cannot be recovered from.
