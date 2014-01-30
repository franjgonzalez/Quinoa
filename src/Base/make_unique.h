//******************************************************************************
/*!
  \file      src/Base/make_unique.h
  \author    J. Bakosi
  \date      Sat 25 Jan 2014 02:48:27 PM MST
  \copyright Copyright 2005-2012, Jozsef Bakosi, All rights reserved.
  \brief     Define make_unique for unique_ptr until C++14
  \details   Define make_unique for unique_ptr until C++14
*/
//******************************************************************************
#ifndef make_unique_h
#define make_unique_h

#include <memory>

namespace tk {

// Taken from http://gcc.gnu.org/onlinedocs/libstdc++/latest-doxygen,
// generated on 2014-01-06.

using std::unique_ptr;
using std::remove_extent;

template<typename _Tp>
  struct _MakeUniq
  { typedef unique_ptr<_Tp> __single_object; };

template<typename _Tp>
  struct _MakeUniq<_Tp[]>
  { typedef unique_ptr<_Tp[]> __array; };

template<typename _Tp, size_t _Bound>
  struct _MakeUniq<_Tp[_Bound]>
  { struct __invalid_type { }; };

/// std::make_unique for single objects
template<typename _Tp, typename... _Args>
  inline typename _MakeUniq<_Tp>::__single_object
  make_unique(_Args&&... __args)
  { return unique_ptr<_Tp>(new _Tp(std::forward<_Args>(__args)...)); }

/// std::make_unique for arrays of unknown bound
template<typename _Tp>
  inline typename _MakeUniq<_Tp>::__array
  make_unique(size_t __num)
  { return unique_ptr<_Tp>(new typename remove_extent<_Tp>::type[__num]()); }

/// Disable std::make_unique for arrays of known bound
template<typename _Tp, typename... _Args>
  inline typename _MakeUniq<_Tp>::__invalid_type
  make_unique(_Args&&...) = delete;

} // tk::

#endif // make_unique_h