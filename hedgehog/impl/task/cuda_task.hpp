// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the
// software in any medium, provided that you keep intact this entire notice. You may improve, modify and create
// derivative works of the software or any portion of the software, and you may copy and distribute such modifications
// or works. Modified works should carry a notice stating that you changed the software and should note the date and
// nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the
// source of the software. NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND,
// EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR
// WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE
// CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS
// THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE. You
// are solely responsible for determining the appropriateness of using and distributing the software and you assume
// all risks associated with its use, including but not limited to the risks and costs of program errors, compliance
// with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of
// operation. This software is not intended to be used in any situation where a failure could cause risk of injury or
// damage to property. The software developed by NIST employees is not subject to copyright protection within the
// United States.

#ifndef HEDGEHOG_IMPL_TASK_CUDA_TASK_H
#define HEDGEHOG_IMPL_TASK_CUDA_TASK_H
#ifdef HH_USE_CUDA

#include <cublas_v2.h>
#include <cuda_runtime.h>

#include "../../tool/log.hpp"

#ifndef hh_check_cuda_error
inline void __check_cuda_error(cudaError_t err) {
    if (cudaSuccess != err) {
        log::error("Cuda error = ", err, " \"", cudaGetErrorString(err), "\".");
        exit(43);
    }
}

inline void __check_cuda_error(cublasStatus_t status) {
    if (CUBLAS_STATUS_SUCCESS != status) {
        log::error("Cublas error = ", err, ".");
        exit(44);
    }
}

#ifdef HH_ENABLE_CHECK_CUDA
#define hh_check_cuda_error(err) __check_cuda_error(err)
#else //HH_ENABLE_CHECK_CUDA
#define hh_check_cuda_error(err) err
#endif //HH_ENABLE_CHECK_CUDA

#endif //hh_check_cuda_error

namespace hh {

class CudaTask {
    bool enable_peer_access_ = false;
    std::unordered_set<int> peer_device_ids_ = {};
    cudaStream_t stream_ = {};

  public:
    CudaTask() = default;
    explicit CudaTask(bool enable_peer_access) : enable_peer_access_(enable_peer_access) {}

    void initialize(auto ctx) {
        int num_gpus = 0;
        int can_access = 0;
        hh_check_cuda_error(cudaGetDeviceCount(&num_gpus));
        assert(ctx->device_id() < num_gpus);
        hh_check_cuda_error(cudaSetDevice(ctx->device_id()));
        hh_check_cuda_error(cudaStreamCreate(&stream_));

        if (enable_peer_access_) {
            for (int i = 0; i < num_gpus; ++i) {
                if (i != ctx->device_id()) {
                    hh_check_cuda_error(cudaDeviceCanAccessPeer(&can_access, ctx->device_id(), i));

                    if (can_access) {
                        auto ret = cudaDeviceEnablePeerAccess(i, 0);
                        if (ret != cudaErrorPeerAccessAlreadyEnabled) {
                            hh_check_cuda_error(ret);
                        }
                        peer_device_ids_.insert(i);
                    }
                }
            }
        }
        auto ret = cudaGetLastError();
        if (ret != cudaErrorPeerAccessAlreadyEnabled) {
            hh_check_cuda_error(ret);
        }
        this->initialize_cuda();
    }

    void finalize() {
        this->finalize_cuda();
        hh_check_cuda_error(cudaStreamDestroy(stream_));
    }

    virtual void initialize_cuda() {}
    virtual void finalize_cuda() {}

    bool enable_peer_access() const { return enable_peer_access_; }
    bool has_peer_access(int device_id) { return peer_device_ids_.find(device_id) != peer_device_ids_.end(); }
    cudaStream_t stream() const { return stream_; }
};

} // end namespace hh

#endif
#endif
