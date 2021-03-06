#pragma once
/*
**  Copyright (C) 2013 Aldebaran Robotics
**  See COPYING for the license
*/

#ifndef _QI_TYPE_TYPEDISPATCHER_HPP_
#define _QI_TYPE_TYPEDISPATCHER_HPP_

#include <qi/type/typeinterface.hpp>

namespace qi {

  /** Invoke one of the visitor functions in dispatcher based on kind().
   * Dispatcher must implement TypeDispatcher.
   */
  template<typename Dispatcher>
  Dispatcher& typeDispatch(Dispatcher& dispatcher, AnyReference value);


  // class QI_API TypeDispatcher
  // {
  // public:
  //   void visitUnknown(qi::AnyReference value);
  //   void visitVoid();
  //   void visitInt(qi::int64_t value, bool isSigned, int byteSize);
  //   void visitFloat(double value, int byteSize);
  //   void visitString(char* data, size_t size);
  //   void visitList(qi::AnyIterator begin, qi::AnyIterator end);
  //   void visitMap(qi::AnyIterator begin, qi::AnyIterator end);
  //   void visitObject(qi::GenericObject value);
  //   void visitPointer(qi::AnyReference pointee);
  //   void visitTuple(const std::string &className, const std::vector<qi::AnyReference>& tuple, const std::vector<std::string>& elementNames);
  //   void visitDynamic(qi::AnyReference pointee);
  //   void visitRaw(qi::AnyReference value);
  //   void visitIterator(qi::AnyReference value);
  //   void visitAnyObject(qi::AnyObject& ptr);
  //   void visitOptional(qi::AnyReference value);
  // };

}

#include <qi/type/detail/typedispatcher.hxx>

#endif  // _QITYPE_TYPEDISPATCHER_HPP_
