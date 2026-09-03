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

#ifndef HEDGEHOG_TOOL_TYPE_LIST_H
#define HEDGEHOG_TOOL_TYPE_LIST_H

// TODO: type lists operations should be rewritten using std::meta::info from C++26 to improve compile time.

namespace hh {

//
// The type list is a structure that just contains a list of types.
//
// using inputs = hh::type_list<int, float, double>;
//

template <typename ...Ts>
struct type_list {};

// size ////////////////////////////////////////////////////////////////////////

//
// Get the number of types in the list.
//

template <typename L>
struct type_list_size_impl;

template <typename ...Ts>
struct type_list_size_impl<type_list<Ts...>> {
    static constexpr size_t value = sizeof...(Ts);
};

template <typename L>
constexpr size_t type_list_size = type_list_size_impl<L>::value;

// append //////////////////////////////////////////////////////////////////////

//
// Appends a new type to the list:
//
// using list = hh::type_list<int, float>
// hh::type_list_append<list, NewType>   ->   hh::type_list<int, float, NewType>
//

template <typename L, typename ...Types>
struct type_list_append_impl;

template <typename ...Types, typename ...Ts>
struct type_list_append_impl<type_list<Ts...>, Types...> {
    using type = type_list<Ts..., Types...>;
};

template <typename L, typename ...Types>
using type_list_append = typename type_list_append_impl<L, Types...>::type;

// prepend /////////////////////////////////////////////////////////////////////

//
// Prepends a new type to the list:
//
// using list = hh::type_list<int, float>
// hh::type_list_prepend<list, NewType>   ->   hh::type_list<NewType, int, float>
//

template <typename L, typename ...Types>
struct type_list_prepend_impl;

template <typename ...Types, typename ...Ts>
struct type_list_prepend_impl<type_list<Ts...>, Types...> {
    using type = type_list<Types..., Ts...>;
};

template <typename L, typename ...Types>
using type_list_prepend = typename type_list_prepend_impl<L, Types...>::type;

// dispatch ////////////////////////////////////////////////////////////////////

//
// Dispatch types inside a list into the give template. It is also possible to
// specify more types to prepend to the template:
//
// using list = hh::type_list<int, float>
//
// template <typename Inputs...>                  struct MyTemplate1 {};
// template <typename Config, typename Inputs...> struct MyTemplate2 {};
//
// hh::type_list_dispatch<list, MyTemplate1>             ->   MyTemplate1<int, float>
// hh::type_list_dispatch<list, MyTemplate2, MyConfig>   ->   MyTemplate2<MyConfig, int, float>
//

template <typename L, template <typename ...> class T, typename ...Types>
struct type_list_dispatch_impl;

template <template <typename ...> class T, typename ...Ts, typename ...Types>
struct type_list_dispatch_impl<type_list<Ts...>, T, Types...> {
    using type = T<Types..., Ts...>;
};

template <typename L, template <typename ...> class T, typename ...Types>
using type_list_dispatch = typename type_list_dispatch_impl<L, T, Types...>::type;

// apply ///////////////////////////////////////////////////////////////////////

//
// Apply a given template to the types in the list:
//
// using list = hh::type_list<int, float>
// hh::type_list_apply<list, std::shared_ptr>   ->   hh::type_list<std::shared_ptr<int>, std::shared_ptr<double>>
//

template <typename L, template <typename> class T>
struct type_list_apply_impl;

template <template <typename> class T, typename ...Ts>
struct type_list_apply_impl<type_list<Ts...>, T> {
    using type = type_list<T<Ts>...>;
};

template <typename L, template <typename> class T>
using type_list_apply = typename type_list_apply_impl<L, T>::type;

//
// Add * to every type in the list:
//
// using list = hh::type_list<int, float>
// hh::type_list_apply_ptr<list>   ->   hh::type_list<int *, float *>
//

template <typename L>
struct type_list_apply_ptr_impl;

template <typename ...Ts>
struct type_list_apply_ptr_impl<type_list<Ts...>> {
    using type = type_list<Ts *...>;
};

template <typename L>
using type_list_apply_ptr = typename type_list_apply_ptr_impl<L>::type;

// contains ////////////////////////////////////////////////////////////////////

//
// Check if a type is iside a list:
//
// using list = hh::type_list<int, float>
// hh::type_list_contains<list, int>    -> true
// hh::type_list_contains<list, float>  -> true
// hh::type_list_contains<list, double> -> false
//

template <typename T, typename ...Ts>
struct types_contain_impl {
    static constexpr bool value = false;
};

template <typename T, typename H, typename ...Ts>
struct types_contain_impl<T, H, Ts...> {
    static constexpr bool value = std::is_same_v<T, H> || types_contain_impl<T, Ts...>::value;
};

template <typename T, typename ...Ts>
constexpr bool types_contain = types_contain_impl<T, Ts...>::value;

template <typename L, typename T>
struct type_list_contains_impl;

template <typename T, typename ...Ts>
struct type_list_contains_impl<type_list<Ts...>, T> {
    static constexpr bool value = types_contain_impl<T, Ts...>::value;
};

template <typename L, typename T>
constexpr bool type_list_contains = type_list_contains_impl<L, T>::value;

// map /////////////////////////////////////////////////////////////////////////

//
// Map a template function to all the types in the list:
//
// using list = hh::type_list<int, float>
// hh::type_list_map<list>([&]<typename T>() {
//     do_something_with_t<T>();
// });
//
// is equivalent to:
//
// do_something_with_t<int>();
// do_something_with_t<float>();
//

template <typename T, typename ...Ts>
constexpr void type_list_map(type_list<T, Ts...>, auto function) {
    function.template operator()<T>();
    if constexpr (sizeof...(Ts) > 0) {
        type_list_map(type_list<Ts...>(), function);
    }
}

template <typename L>
constexpr void type_list_map(auto function) {
    type_list_map(L(), function);
}

} // end namespace hh

#endif
