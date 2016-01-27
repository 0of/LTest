/*
* LTest
*
* Copyright (c) 2016 "0of" Magnus
* Licensed under the MIT license.
* https://github.com/0of/LTest/blob/master/LICENSE
*/
#ifndef SPEC_INITIALIZER_H
#define SPEC_INITIALIZER_H

#include <iterator>
#include <type_traits>

template<typename Spec>
class SpecInitializer {
private:
  template<typename T>
  using return_iterator_category_t = typename decltype(std::declval<T>().begin())::iterator_category;

  template<typename iteratorTag>
  using is_input_iterator_t = std::is_base_of<std::input_iterator_tag, iteratorTag>;

private:
  template<typename Type>
  struct is_container {
  private:
    template<typename T>
    static auto check(T*) -> typename is_input_iterator_t<return_iterator_category_t<T>>::type;
    template<typename T> static auto check(...) -> std::false_type;
  
  public:
    static constexpr bool value = decltype(check<std::remove_reference_t<Type>>(nullptr))::value;
  };

private:
  std::add_lvalue_reference_t<Spec> _spec;

public:
  explicit SpecInitializer(std::add_lvalue_reference_t<Spec> spec)
    : _spec{ spec }
  {}

public:
  template<typename Any>
  void appendCases(Any&& any) {
    appendCase(std::forward<Any>(any));
  }

  template<typename Any, typename... Rest>
  void appendCases(Any&& any, Rest&&... rest) {
    appendCases(std::forward<Any>(any));
    appendCases(std::forward<Rest>(rest)...);
  }

private:
  template<typename Container>
  std::enable_if_t<is_container<Container>::value> appendCase(Container&& container) {
    for (const auto& fn : container) {
       appendCases(fn);
    }
  }

  template<typename Functor>
  std::enable_if_t<!is_container<Functor>::value> appendCase(Functor&& fn) {
     fn(_spec);
  }
};

#endif // SPEC_INITIALIZER_H
