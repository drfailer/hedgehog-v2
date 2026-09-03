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

#include <cstddef>
#include <utility>

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
constexpr bool types_contain = (std::is_same_v<T, Ts> || ...);

template <typename L, typename T>
struct type_list_contains_impl;

template <typename T, typename ...Ts>
struct type_list_contains_impl<type_list<Ts...>, T> {
    static constexpr bool value = types_contain<T, Ts...>;
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

template <typename ...Ts>
constexpr void type_list_map_impl(type_list<Ts...>, auto function) {
    (function.template operator()<Ts>(), ...);
}

template <typename L>
constexpr void type_list_map(auto function) {
    type_list_map_impl(L{}, function);
}

// type at /////////////////////////////////////////////////////////////////////

#if __has_builtin(__type_pack_element)

//
// We use a compiler builtin (supported by Clang, GCC 9+, MSVC) to extract
// types. It is supposed to be faster than using recursion and it doesn't
// involve including massive headers like tuple (tuple_element).
//
// Clang also has __make_integer_seq that we could use.
//

template <size_t I, typename... Ts>
using type_at = __type_pack_element<I, Ts...>;

#else

// fallback to standard recusion approach

template <size_t I, typename T>
struct type_at_impl;

template <size_t I, typename T, typename... Ts>
struct type_at_impl<I, type_list<T, Ts...>> {
    using type = typename type_at_impl<I - 1, type_list<Ts...>>::type;
};

template <typename T, typename... Ts>
struct type_at_impl<0, type_list<T, Ts...>> {
    using type = T;
};

// Interface
template <size_t I, typename... Ts>
using type_at = typename type_at_impl<I, type_list<Ts...>>::type;

#endif

// io types ////////////////////////////////////////////////////////////////////

//
// Split list of types based on a separator (Hedgehog-v1 api):
//
// using io = hh::io_types<1, int ,float>
// io::inputs    ->   hh::type_list<int>
// io::outputs   ->   hh::type_list<float>
//

namespace io_types_helpers {
    template <size_t Offset, typename Seq>
    struct shift_seq;

    template <size_t Offset, size_t... Is>
    struct shift_seq<Offset, std::index_sequence<Is...>> {
        using type = std::index_sequence<(Offset + Is)...>;
    };

    template <typename Seq, typename... Ts>
    struct build_list;

    template <size_t... Is, typename... Ts>
    struct build_list<std::index_sequence<Is...>, Ts...> {
        using type = type_list<type_at<Is, Ts...>...>;
    };
}

template <size_t Separator, typename... Ts>
struct io_types {
    static_assert(Separator <= sizeof...(Ts), "Separator exceeds type pack size");

    using input_seq  = std::make_index_sequence<Separator>;
    using output_seq = typename io_types_helpers::shift_seq<
        Separator,
        std::make_index_sequence<sizeof...(Ts) - Separator>
    >::type;

    using inputs  = typename io_types_helpers::build_list<input_seq, Ts...>::type;
    using outputs = typename io_types_helpers::build_list<output_seq, Ts...>::type;
};

} // end namespace hh

#endif
