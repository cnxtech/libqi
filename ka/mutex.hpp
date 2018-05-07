#ifndef KA_MUTEX_HPP
#define KA_MUTEX_HPP
#pragma once
#include <mutex>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/synchronized_value.hpp>
#include "typetraits.hpp"

namespace ka {
  namespace traits {
    namespace detail {
      template<typename M> struct IsMutex : False {};

      // Common mutexes that might be used in this library.
      template<> struct IsMutex<std::mutex>                   : True {};
      template<> struct IsMutex<std::recursive_mutex>         : True {};
#if !BOOST_OS_ANDROID
      template<> struct IsMutex<std::timed_mutex>             : True {};
      template<> struct IsMutex<std::recursive_timed_mutex>   : True {};
#endif
      template<> struct IsMutex<boost::mutex>                 : True {};
      template<> struct IsMutex<boost::recursive_mutex>       : True {};
      template<> struct IsMutex<boost::timed_mutex>           : True {};
      template<> struct IsMutex<boost::recursive_timed_mutex> : True {};
      template<> struct IsMutex<boost::shared_mutex>          : True {};
    } // namespace detail

    /// True if T is one of the commonly used mutex types:
    /// std::mutex, std::recursive_mutex, std::timed_mutex, std::recursive_timed_mutex, boost::mutex
    /// boost::recursive_mutex, boost::timed_mutex, boost::recursive_timed_mutex, boost::shared_mutex
    template<typename T>
    using IsMutex = typename detail::IsMutex<T>::type;
  } // namespace detail
} // namespace ka

namespace std {
  /// model ScopeLockable M:
  /// Mutex M
  template<typename M, typename = ka::traits::EnableIf<ka::traits::IsMutex<M>::value>>
  std::unique_lock<M> scopelock(M& m) {
    return std::unique_lock<M>{ m };
  }
} // namespace std

namespace boost {
  /// model ScopeLockable M:
  /// Mutex M
  template<typename M, typename = ka::traits::EnableIf<ka::traits::IsMutex<M>::value>>
  std::unique_lock<M> scopelock(M& m) { // std::unique_lock works for boost mutexes
    return std::unique_lock<M>{ m };
  }

  /// model ScopeLockable boost::synchronized_value<T>:
  template<typename T>
  auto scopelock(synchronized_value<T>& sync_val)
      -> decltype(sync_val.synchronize()) { // TODO: Remove this when we can use C++14
    return sync_val.synchronize();
  }
} // namespace boost

#endif // QI_MUTEX_HPP
