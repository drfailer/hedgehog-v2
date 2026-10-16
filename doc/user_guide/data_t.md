# The `data_t` type

When creating task, we are often using `data_t`, which is a wrapper that allows
keeping similar APIs for different data modes. There are 3 data modes:

1. The shared pointer mode: used by default (similar to Hedgehog v1). The data
   flowing through the graph is always allocation using a shared pointer which
   makes memory management easy when prototyping.
2. The pointer mode: every data flowing through the graph is a pointer. This is
   a middle ground as it forces users to manage the memory manually (we have
   allocators...), but prevents issues when copying data.
3. The value mode: gives full control over the type. There is non compatibility
   with the other modes (you have to change the type list).

The default mode is the shared pointer mode because we noticed that on most
graphs, the atomic overhead was negligible when tasks do sufficient work. We
also make sure that there are very little shared pointer copies under the hood
(a shared pointer is copied only when it is sent to multiple nodes).

If you think that shared pointer cause performance issues (or you simply don't
like them), you can use the other modes:

- define `HH_POINTER_MODE` to use the pointer mode.
- define `HH_VALUE_MODE` to use the value mode.

## Data movement

When a data is transferred to 1 receiver node, it is moved. When it transferred
to N receiver nodes, it is copied (N - 1) times and moved once.
