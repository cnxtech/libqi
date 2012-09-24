#pragma once
/*
**  Copyright (C) 2012 Aldebaran Robotics
**  See COPYING for the license
*/

#ifndef _QIMESSAGING_FUTURE_HPP_
#define _QIMESSAGING_FUTURE_HPP_

#include <vector>
#include <qimessaging/api.hpp>
#include <boost/shared_ptr.hpp>
#include <qimessaging/signal.hpp>

namespace qi {

  template<typename T> struct FutureType
  {
    typedef T type;
  };

  // Hold a void* for Future<void>
  template<> struct FutureType<void>
  {
    typedef void* type;
  };

  template <typename T> class FutureInterface;
  template <typename T> class Future;
  template <typename T> class FutureSync;
  template <typename T> class Promise;

  namespace detail {
    template <typename T> class FutureState;
  }

  template <typename T>
  class Future {
  public:
    typedef typename FutureType<T>::type ValueType;
    Future()
      : _p(new detail::FutureState<T>())
    {
    }

    Future(const FutureSync<T>& b)
    {
      *this = b;
    }

    Future<T>& operator = (const FutureSync<T>& b)
    {
      b._sync = false;
      _p = b._p;
      return *this;
    }

    explicit Future<T>(const ValueType& v)
    {
      Promise<T> promise;
      promise.setValue(v);
      *this = promise.future();
    }

    const ValueType &value() const    { return _p->value(); }
    ValueType &value()                { return _p->value(); }
    operator const ValueType&() const { return _p->value(); }
    operator ValueType&()             { return _p->value(); }

    bool wait(int msecs = 30000) const         { return _p->wait(msecs); }
    bool isReady() const                       { return _p->isReady(); }
    bool hasError() const                      { return _p->hasError(); }

    const std::string &error() const           { return _p->error(); }


    FutureSync<T> sync()
    {
      return FutureSync<T>(*this);
    };

  public: //Signals
    unsigned int connect(boost::function<void (qi::Future<T>)> fun, qi::EventLoop *evLoop = 0) { _p->connect(*this, fun, evLoop); }
    bool disconnect(unsigned int i) { _p->disconnect(i); }
    //qi::Signal<void (qi::Future<T>)> &onResult() { return _p->_onResult; }

  protected:
    boost::shared_ptr< detail::FutureState<T> > _p;
    friend class Promise<T>;
    friend class FutureSync<T>;
  };

  template<typename T> class FutureSync: public Future<T>
  {
  public:
    // This future cannot be set, so sync starts at false
    FutureSync() : _sync(false) {}

    FutureSync(const Future<T>& b)
    : _sync(true)
    {
      *this = b;
      this->_p = b._p;
    }

    FutureSync(const FutureSync<T>& b)
    : _sync(true)
    {
      *this = b;
      this->_p = b._p;
      b._sync = false;
    }

    explicit FutureSync<T>(const typename Future<T>::ValueType& v)
    : _sync(false)
    {
      Promise<T> promise;
      promise.setValue(v);
      *this = promise.future();
    }

    FutureSync<T>& operator = (const FutureSync<T>& b)
    {
      this->_p = b._p;
      _sync = true;
      b._sync = false;
      return *this;
    }

    FutureSync<T>& operator = (const Future<T>& b)
    {
     this->_p = b._p;
      _sync = true;
      return *this;
    }

    ~FutureSync()
    {
      if (_sync)
        this->wait();
    }

    Future<T> async()
    {
      return *this;
    }

  private:
    mutable bool _sync;
    friend class Future<T>;
  };


  template <typename T>
  class Promise {
  public:
    typedef typename FutureType<T>::type ValueType;

    Promise() { }

    void setValue(const ValueType &value) {
      _f._p->setValue(_f, value);
    }

    void setError(const std::string &msg) {
      _f._p->setError(_f, msg);
    }

    void reset() {
      _f._p->reset();
    }

    Future<T> future() { return _f; }

  protected:
    Future<T> _f;
  };
};

#include <qimessaging/details/future.hxx>

#endif  // _QIMESSAGING_FUTURE_HPP_
