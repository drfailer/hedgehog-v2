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

#ifndef HEDGEHOG_TOOL_CONCEPTS_H
#define HEDGEHOG_TOOL_CONCEPTS_H

namespace hh {

// Component lifecycle /////////////////////////////////////////////////////////

template <typename C>
concept Copyable = requires(C &c) { c.copy(); };

template <typename C, typename Info>
concept InitializableWith = requires(C &c, Info &i) { c.initialize(i); };

template <typename C>
concept Initializable = requires(C &c) { c.initialize(); };

template <typename C, typename Info>
concept FinalizableWith = requires(C &c, Info &i) { c.finalize(i); };

template <typename C>
concept Finalizable = requires(C &c) { c.finalize(); };

// Task execution //////////////////////////////////////////////////////////////

template <typename T, typename Ctx, typename Data>
concept ExecutableWithContext = requires(T &t, Ctx *ctx, Data d) { t.execute(ctx, d); };

// States //////////////////////////////////////////////////////////////////////

template <typename S>
concept Lockable = requires(S &s) {
    s.lock();
    s.unlock();
};

// Executor capabilities ///////////////////////////////////////////////////////

template <typename E, typename N, typename I>
concept HasOnTransfer = requires(E &e, N *n, I const &i) { e.on_transfer(n, i); };

template <typename E>
concept HasOnResult = requires(E &e) { e.on_result(); };

// Graph structure /////////////////////////////////////////////////////////////

template <typename N>
concept HasInputNodes = requires(N &n) { n.input_nodes(); };

// Memory //////////////////////////////////////////////////////////////////////

template <typename T>
concept HasCleanMemory = requires(T &t) { t.clean_memory(); };

// Config deduction ////////////////////////////////////////////////////////////

template <typename Impl>
concept HasNodeInput = requires { typename Impl::node_input; };

template <typename Impl>
concept HasNodeOutput = requires { typename Impl::node_output; };

template <typename Impl>
concept HasExecutor = requires { typename Impl::executor; };

template <typename T>
concept HasIO = requires { typename T::io; };

} // end namespace hh

#endif
