# Cuda Task

The `CudaTask` is a helper task that will manage a cuda stream.

```cpp
struct MyTask : hh::CudaTask {
    using inputs = hh::type_list<MatrixTile>;
    using outputs = hh::type_list<MatrixTile>;

    void execute(auto ctx, data_t<MatrixTile> data) {
        lauch_compute_kernel(this->stream(), data);
        ctx->push_result(std::move(data));
    }

    // this is optional (default is no-op)

    void initialize_cuda() override {
        // one may create a cublas handle here
    }

    void finalize_cuda() override {
        // ...
    }
};
```

- Use `hh::CudaTask::stream()` to access the cuda stream for this task.
- Optinaly override `initialize_cuda` and/or `finalize_cuda` to manage data
  initialization or finalization.
- `hh::CudaTask` constructor can also accept a `enable_peer_access` flag.
