# Hedgehog User Guide

Hedgehog is an HPC library that helps developers managing coarse grained
parallelism using a data-flow graph approach. The developer creates tasks that
can be single-threaded or multi-threaded. Each task takes consumes and process
different input types and produces data of different output types. The task is
then managed by a graph node linked to other nodes within a graph that will
manage the data transfer.

Hedgehog is part of the Asynchronous Many-Tasks systems (AMT), which are
opposed to bulk synchronous systems like OpenMP. The data-flow approach helps
reducing sequential barriers (every synchronization point is explicit) and
maximizes computation overlap as the data flows through the graph without
restriction. Each node runs on one or multiple threads and producing a result
data will automatically transfer the data to the receiving nodes and wake up
the threads. For example, a simulation code would decompose the domain into
meshes, and process each mesh or sub-mesh in parallel and only synchronize when
it is necessary (halo exchange for instance).

This library excels at coarse-grained parallelism, but it is not thought to
handle fine-grained parallelism (the tasks need to do sufficient work to yield
good performance). However, Hedgehog was thought to be used alongside GPUs
which generally outperform CPUs when it comes to fine grained parallelism. When
using GPUs, Hedgehog can be used as a global scheduler that will help
organizing the code and maximize device utilization through computation
overlap.

## Graph nodes

- [task](task.md)
- [state managers](state_managers.md)
- [graph](graph.md)
- [execution_pipelines](execution_pipelines.md)

## Builtin tasks

- [cuda task](cuda-task.md)

## Memory

- [memory pools](memory_pools.md)
- [allocators](allocators.md)

## Tools

- [data_t](data_t.md)
- [profiling](profiling.md)
- [graph view](graph_view.md)
