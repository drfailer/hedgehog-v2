# Cuda Task

The `CudaTask` is a helper base class that manages a CUDA stream and
(optionally) peer access between GPUs. A task inheriting from `CudaTask` gets
automatic stream creation, device setup, and cleanup.

## Basic usage

```cpp
struct MyTask : hh::CudaTask {
    using inputs = hh::type_list<MatrixTile>;
    using outputs = hh::type_list<MatrixTile>;

    void execute(auto ctx, data_t<MatrixTile> data) {
        launch_compute_kernel(this->stream(), data);
        ctx->push_result(std::move(data));
    }
};
```

- Use `this->stream()` to access the CUDA stream managed by the task.
- `CudaTask` provides `initialize(auto ctx)` which calls `cudaSetDevice`,
  creates the stream, and optionally enables peer access. User code should
  **not** override `initialize` directly — use `initialize_cuda` instead.
- `CudaTask` provides `finalize()` which calls `finalize_cuda` then destroys
  the stream.

## Constructor

```cpp
CudaTask();                                 // default: no peer access
explicit CudaTask(bool enable_peer_access); // enable GPU peer access
```

When `enable_peer_access` is `true`, the task will call
`cudaDeviceEnablePeerAccess` for every other GPU that supports it during
initialization.

## Lifecycle hooks

Override `initialize_cuda` and/or `finalize_cuda` to run custom setup/teardown
code (e.g. creating a cuBLAS handle). Both are optional and default to no-ops.

```cpp
struct MyTask : hh::CudaTask {
    using inputs = hh::type_list<MatrixTile>;
    using outputs = hh::type_list<MatrixTile>;

    cublasHandle_t handle_;

    MyTask() : CudaTask(/* enable_peer_access = */ true) {}

    void initialize_cuda() override {
        cublasCreate(&handle_);
        cublasSetStream(handle_, this->stream());
    }

    void finalize_cuda() override {
        cublasDestroy(handle_);
    }

    void execute(auto ctx, data_t<MatrixTile> data) {
        // use handle_ and this->stream()
        ctx->push_result(std::move(data));
    }
};
```

> **Note:** `initialize_cuda` and `finalize_cuda` do not receive a context
> parameter. The device is already set via `cudaSetDevice` before
> `initialize_cuda` is called.

## Peer access

```cpp
bool enable_peer_access() const;   // was peer access requested?
bool has_peer_access(int device_id); // can this task access device_id's memory?
```

## Error checking

Hedgehog provides the `hh_check_cuda_error` macro for checking CUDA and cuBLAS
return codes:

```cpp
hh_check_cuda_error(cudaMalloc(&ptr, size));
hh_check_cuda_error(cublasCreate(&handle));
```

Error checking is only active when `HH_ENABLE_CHECK_CUDA` is defined. Without
it, the macro evaluates to the bare expression (no check). Define it in your
build system for debug builds:

```cmake
target_compile_definitions(my_target PRIVATE HH_ENABLE_CHECK_CUDA)
```

## Multi-GPU with execution pipelines

The primary use case for `CudaTask` is running the same graph on multiple GPUs
via an [execution pipeline](execution_pipelines.md). Each pipeline graph is
assigned a different device through `PipelineInfo`:

```cpp
struct GpuCompute : hh::CudaTask {
    using inputs = hh::type_list<MatrixTile>;
    using outputs = hh::type_list<MatrixTile>;

    GpuCompute() : CudaTask(/* enable_peer_access = */ true) {}

    void execute(auto ctx, data_t<MatrixTile> data) {
        launch_kernel(this->stream(), data);
        ctx->push_result(std::move(data));
    }
};

struct GpuPipeline {
    auto make_graph(size_t graph_index) {
        auto graph = hh::make_graph<1, MatrixTile, MatrixTile>("GPU Graph");
        auto compute = hh::make_node<GpuCompute>("Compute");
        graph->inputs(compute);
        graph->outputs(compute);
        return graph;
    }

    template <typename T>
    size_t send_to(hh::data_t<T> data) {
        return data->target_device;
    }
};

// create pipeline with 2 GPUs
std::vector<hh::PipelineInfo> config = {
    {.numa_id = 0, .device_id = 0},
    {.numa_id = 0, .device_id = 1},
};
auto pipeline = hh::make_pipeline<GpuPipeline>(config, "GPU Pipeline");
```
