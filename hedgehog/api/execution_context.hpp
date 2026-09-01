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

#ifndef HEDGEHOG_API_EXECUTION_CONTEXT_H
#define HEDGEHOG_API_EXECUTION_CONTEXT_H

namespace hh {

template <typename NodeType>
class ExecutionContext {
  private:
    friend NodeType;
    NodeType *node_;
    RuntimeInfo info_;

    // constructor used by the node
    void ctx(NodeType *node, RuntimeInfo const &info) {
        node_ = node;
        info_ = info;
    }

  public:
    std::string const &name() { return info_.node.name; }
    std::string const &graph_name() { return info_.graph.name; }
    int &graph_id() { return info_.graph.id; }
    size_t number_thread() { return info_.node.number_threads; }
    size_t thread_index() { return info_.exec.thread_index; }
    int numa_id() { return info_.exec.numa_id; }
    int rank() { return info_.exec.rank; }
    int device_id() { return info_.exec.device_id; }

    template <typename T>
    void push_data(std::shared_ptr<T> data) {
        node_->push_data(data, info_);
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data) {
        node_->push_result(data, info_);
    }
};

}

#endif
