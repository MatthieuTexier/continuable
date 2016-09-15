
/**
 * Copyright 2015-2016 Denis Blank <denis.blank@outlook.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 /*
#include <tuple>
#include <type_traits>
#include <string>

namespace detail {
  template<typename... T>
  struct Identity { };

  template<typename T, typename... Rest>
  struct IdentityInheritenceWrapper
    : Identity<T>, IdentityInheritenceWrapper<Rest...> { };

  enum class NamedParameterId {
    NAMED_PARAMATER_CALLBACK,
    NAMED_PARAMATER_REJECTOR
  };

  template <NamedParameterId Value, typename T>
  struct NamedParameter
    : std::integral_constant<NamedParameterId, Value>,
      std::common_type<T> { };

  template <NamedParameterId Value, typename Default, typename... Args>
  using GetNamedParameterOrDefault = void;

  template<typename, typename>
  class ContinuableBase;

  template<typename T>
  struct ReturnTypeToContinuableConverter;

  template<typename... Args>
  struct 


  template<typename... Args, typename CallbackType>
  class ContinuableBase<Identity<Args...>, CallbackType> {
    CallbackType callback_;

  public:
    explicit ContinuableBase(CallbackType&& callback)
      : callback_(std::move(callback)) { }

    void via() { }

    template<typename C>
    auto then(C&& continuation) {
      // The type the callback will evaluate to
      using EvaluatedTo = decltype(std::declval<C>()(std::declval<Args>()...));


      return EvaluatedTo{ };
    }
  };
} // namespace detail

using namespace detail;

// template<typename... Args>
// using Continuable = detail::ContinuableBase<detail::Identity<Args...>>;

template <typename... Args>
struct Callback {
  void operator() (Args... ) { }
};

template <typename... Args>
auto make_continuable(Args&&...) {
  return Continuable<> { };
}

auto http_request(std::string url) {
  return make_continuable([url](auto& callback) {

    callback("<br>hi<br>");
  });
}

 template<typename Continuation, typename Handler>
auto appendHandlerToContinuation(Continuation&& cont, Handler&& handler) {
  return [cont = std::forward<Continuation>(cont),
          handler = std::forward<Handler>(handler)](auto&& continuation) {
    using T = decltype(continuation);
    return [continuation = std::forward<T>(continuation)](auto&&... arg) {
      continuation(std::forward<decltype(arg)>(arg)...);
    };

    current([continuation = std::forward<T>(continuation)](auto&&... arg) {
      continuation(std::forward<decltype(arg)>(arg)...);
    });
  };
} */

#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <string>
#include <memory>

struct SelfDispatcher {
  template<typename T>
  void operator() (T&& callable) const {
    std::forward<T>(callable)();
  }
};

template<typename Config>
class ContinuableBase;

static auto createEmptyContinuation() {
  return [](auto&& callback) { callback(); };
}

static auto createEmptyCallback() {
  return [](auto&&...) { };
}

template<typename S, unsigned... I, typename T, typename F>
auto applyTuple(std::integer_sequence<S, I...>, T&& tuple, F&& function) {
  return std::forward<F>(function)(std::get<I>(std::forward<T>(tuple))...);
}

class Ownership {
public:
  Ownership() { }
  explicit Ownership(bool isOwning_) : isOwning(isOwning_) { }
  Ownership(Ownership const&) = default;
  explicit Ownership(Ownership&& right) noexcept
    : isOwning(std::exchange(right.isOwning, false)) { };
  Ownership& operator = (Ownership const&) = default;
  Ownership& operator = (Ownership&& right) noexcept {
    isOwning = std::exchange(right.isOwning, false);
    return *this;
  }

  Ownership operator&& (Ownership right) const {
    return Ownership(hasOwnership() && right.hasOwnership());
  }

  bool hasOwnership() const noexcept {
    return isOwning;
  }
  void invalidate() {
    isOwning = false;
  }
private:
  bool isOwning{ true };
};

/// Decorates single values
template<typename Value>
struct CallbackResultDecorator {
  template<typename Callback>
  static auto decorate(Callback&& callback) {
    return [callback = std::forward<Callback>(callback)](auto&&... args) {
      Value value = callback(std::forward<decltype(args)>(args)...);
      return [value =  std::move(value)](auto&& callback) mutable {
        callback(std::move(value));
      };
    };
  }
};

/// No decoration is needed for continuables
template<typename Decorator>
struct CallbackResultDecorator<ContinuableBase<Decorator>>{
  template<typename Callback>
  static auto decorate(Callback&& callback) {
    return std::forward<Callback>(callback);
  }
};

/// Decorates void as return type
template<>
struct CallbackResultDecorator<void> {
  template<typename Callback>
  static auto decorate(Callback&& callback) {
    return [callback = std::forward<Callback>(callback)](auto&&... args) {
      callback(std::forward<decltype(args)>(args)...);
      return createEmptyContinuation();
    };
  }
};

// Decorates tuples as return type
template<typename... Results>
struct CallbackResultDecorator<std::tuple<Results...>> {
  template<typename Callback>
  static auto decorate(Callback&& callback) {
    return [callback = std::forward<Callback>(callback)](auto&&... args) {
      // Receive the tuple from the callback
      auto result = callback(std::forward<decltype(args)>(args)...);
      return [result = std::move(result)] (auto&& next) mutable {
        // Generate a sequence for tag dispatching
        auto constexpr const sequence
          = std::make_integer_sequence<unsigned, sizeof...(Results)>{};
        // Invoke the callback with the tuple returned
        // from the previous callback.
        applyTuple(sequence, std::move(result),
                   std::forward<decltype(next)>(next));
      };
    };
  }
};

/// Create the proxy callback that is responsible for invoking
/// the real callback and passing the next continuation into
/// the result of the following callback.
template<typename Callback, typename Next>
auto createProxyCallback(Callback&& callback,
                         Next&& next) {
  return [callback = std::forward<Callback>(callback),
          next = std::forward<Next>(next)] (auto&&... args) mutable {
    // Callbacks shall always return a continuation,
    // if not, we need to decorate it.
    using Result = decltype(callback(std::forward<decltype(args)>(args)...));
    using Decorator = CallbackResultDecorator<Result>;
    Decorator::decorate(std::move(callback))
            (std::forward<decltype(args)>(args)...)(std::move(next));
  };
}

template<typename Continuation, typename Callback>
auto appendCallback(Continuation&& continuation,
                    Callback&& callback) {
  return [continuation = std::forward<Continuation>(continuation),
          callback = std::forward<Callback>(callback)](auto&& next) mutable {
    // Invoke the next invocation handler
    std::move(continuation)(createProxyCallback(
      std::move(callback), std::forward<decltype(next)>(next)));
  };
}

template<typename Data>
void invokeContinuation(Data data) {
  // Check whether the ownership is acquired and start the continuation call
  if (data.ownership.hasOwnership()) {
    // Pass an empty callback to the continuation to invoke it
    std::move(data.continuation)(createEmptyCallback());
  }
}

template<typename ContinuationType, typename DispatcherType>
struct ContinuableConfig {
  using Continuation = ContinuationType;
  using Dispatcher = DispatcherType;

  template<typename NewType>
  using ChangeContinuationTo = ContinuableConfig<
    NewType, Dispatcher
  >;

  template<typename NewType>
  using ChangeDispatcherTo = ContinuableConfig<
    Continuation, NewType
  >;
};

/// 
template<typename ConfigType>
struct ContinuableData {
  using Config = ConfigType;

  ContinuableData(Ownership ownership_,
                  typename Config::Continuation continuation_,
                  typename Config::Dispatcher dispatcher_) noexcept
    : ownership(std::move(ownership_)),
      continuation(std::move(continuation_)),
      dispatcher(std::move(dispatcher_))  { }

  ContinuableData(typename Config::Continuation continuation_,
                  typename Config::Dispatcher dispatcher_) noexcept
    : continuation(std::move(continuation_)),
      dispatcher(std::move(dispatcher_))  { }

  Ownership ownership;
  typename Config::Continuation continuation;
  typename Config::Dispatcher dispatcher;
};

/// The DefaultDecoration is a container for already materialized
/// ContinuableData which can be accessed instantly.
template<typename Data>
class DefaultDecoration {
public:
  explicit DefaultDecoration(Data data_)
    : data(std::move(data_)) { }

  using Config = typename Data::Config;

  /// Return a r-value reference to the data
  template<typename Callback>
  Data&& undecorate()&& {
    return std::move(data);
  }
  /// Return a copy of the data
  template<typename Callback>
  Data undecorate() const& {
    return data;
  }

private:
  Data data;
};

template<typename Combined>
class LazyCombineDecoration {
public:
  // TODO
  explicit LazyCombineDecoration(Ownership ownership_, Combined combined_)
    : ownership(std::move(ownership_)), combined(std::move(combined_)) { }

  // using Config = typename Data::Config;

  /// Return a r-value reference to the data
  template<typename Callback>
  void undecorate()&& {
  }
  template<typename Callback>
  /// Return a copy of the data
  void undecorate() const& {
  }

  template<typename RightLazyCombine>
  auto merge(RightLazyCombine&& right) {
    
  }

private:
  Ownership ownership;
  Combined combined;
};

template<typename>
struct is_lazy_combined
  : std::false_type { };

template<typename Data>
struct is_lazy_combined<LazyCombineDecoration<Data>>
  : std::true_type { };

template<typename Continuation, typename Dispatcher = SelfDispatcher>
auto make_continuable(Continuation&& continuation,
                      Dispatcher&& dispatcher = SelfDispatcher{}) noexcept {
  using Decoration = DefaultDecoration<ContinuableData<ContinuableConfig<
    std::decay_t<Continuation>,
    std::decay_t<Dispatcher>
  >>>;
  return ContinuableBase<Decoration>(Decoration({
    std::forward<Continuation>(continuation),
    std::forward<Dispatcher>(dispatcher)
  }));
}

template<typename Data, typename Callback>
auto thenImpl(Data data, Callback&& callback) {
  auto next = appendCallback(std::move(data.continuation),
                             std::forward<Callback>(callback));
  using Decoration = DefaultDecoration<ContinuableData<
    typename Data::Config::template ChangeContinuationTo<decltype(next)>
  >>;
  return ContinuableBase<Decoration>(Decoration({
    std::move(data.ownership),
    std::move(next),
    std::move(data.dispatcher)
  }));
}

template<typename Data, typename NewDispatcher>
auto postImpl(Data data ,NewDispatcher&& newDispatcher) {
  using Decoration = DefaultDecoration<ContinuableData<
    typename Data::Config::template
      ChangeDispatcherTo<std::decay_t<NewDispatcher>>
  >>;
  return ContinuableBase<Decoration>(Decoration({
    std::move(data.ownership),
    std::move(data.continuation),
    std::forward<NewDispatcher>(newDispatcher)
  }));
}

template<typename Decoration>
auto convertToLazyCombine(std::true_type /*is_lazy_combined*/,
                          Decoration&& decoration) {
  return std::forward<Decoration>(decoration);
}

template<typename Decoration>
auto convertToLazyCombine(std::false_type /*is_lazy_combined*/,
                          Decoration&& decoration) {
  return LazyCombineDecoration<std::tuple<>>{};
}

template<typename LeftDecoration, typename RightDecoration>
auto combineImpl(LeftDecoration&& leftDecoration,
                 RightDecoration&& rightDecoration) {
  return convertToLazyCombine(is_lazy_combined<std::decay_t<LeftDecoration>>{},
    std::forward<LeftDecoration>(leftDecoration))
      .add(convertToLazyCombine(is_lazy_combined<std::decay_t<RightDecoration>>{},
           std::forward<RightDecoration>(rightDecoration)));
 }

template<typename Decoration>
class ContinuableBase {
  template<typename>
  friend class ContinuableBase;

public:
  explicit ContinuableBase(Decoration decoration_)
    : decoration(std::move(decoration_)) { }

  ~ContinuableBase() {
    // Undecorate/materialize the decoration
    invokeContinuation(std::move(decoration).template undecorate<void(*)()>());
  }
  ContinuableBase(ContinuableBase&&) = default;
  ContinuableBase(ContinuableBase const&) = default;

  template<typename Callback>
  auto then(Callback&& callback)&& {
    return thenImpl(std::move(decoration).template undecorate<Callback>(),
                    std::forward<Callback>(callback));
  }

  template<typename Callback>
  auto then(Callback&& callback) const& {
    return thenImpl(decoration.undecorate<Callback>(),
                    std::forward<Callback>(callback));
  }

  /*template<typename NewDispatcher>
  auto post(NewDispatcher&& newDispatcher)&& {
    return postImpl(std::move(decoration).template undecorate<void>(),
                    std::forward<NewDispatcher>(newDispatcher));
  }

  template<typename NewDispatcher>
  auto post(NewDispatcher&& newDispatcher) const& {
    return postImpl(decoration.template undecorate<void>(),
                    std::forward<NewDispatcher>(newDispatcher));
  }*/

  template<typename RightDecoration>
  auto operator&& (ContinuableBase<RightDecoration>&& right)&& {
    return combineImpl(std::move(decoration), std::move(right.decoration));
  }

  template<typename RightDecoration>
  auto operator&& (ContinuableBase<RightDecoration> const& right)&& {
    return combineImpl(std::move(decoration), right.decoration);
  }

  template<typename RightDecoration>
  auto operator&& (ContinuableBase<RightDecoration>&& right) const& {
    return combineImpl(decoration, std::move(right.decoration));
  }

  template<typename RightDecoration>
  auto operator&& (ContinuableBase<RightDecoration> const& right) const& {
    return combineImpl(decoration, right.decoration);
  }

private:
  /// The Decoration represents the possible lazy materialized
  /// data of the continuable.
  /// The decoration pattern is used to make it possible to allow lazy chaining
  /// of operators on Continuables like the and expression `&&`.
  Decoration decoration;
};

static auto makeTestContinuation() {
  return make_continuable([i = std::make_unique<int>(0)](auto&& callback) {
    callback("47");
  });
}

struct Inspector {
  template<typename... Args>
  auto operator() (Args...) {
    return std::common_type<std::tuple<Args...>>{};
  }
};

template<unsigned N>
struct FailIfWrongArgs {
  template<typename... Args>
  auto operator() (Args...)
    -> std::enable_if_t<N == sizeof...(Args)> { }
};

int main(int, char**) {
  auto dispatcher = SelfDispatcher{};

  auto unwrap = [](auto&& callback) {

    callback("47");
  };

  /*auto unwrapper = [](auto&&... args) {
    return std::common_type<std::tuple<decltype(args)...>>{};
  };*/

  using T = decltype(unwrap(FailIfWrongArgs<0>{}));

  // using T = decltype(unwrap(std::declval<Inspector>()));
  // T t{};

  // auto combined = makeTestContinuation() && makeTestContinuation();

  int res = 0;
  makeTestContinuation()
    .then([](std::string /*str*/) {
      return std::make_tuple(47, 46, 45);
    })
    // .post(dispatcher)
    .then([](int val1, int val2, int val3) {
      return val1 + val2 + val3;
    })
    .then([&](int val) {
      res += val;
    });

  return res;
}
