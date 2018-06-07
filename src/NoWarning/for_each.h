// *****************************************************************************
/*!
  \file      src/NoWarning/for_each.h
  \copyright 2012-2015, J. Bakosi, 2016-2018, Los Alamos National Security, LLC.
  \brief     Include boost/mpl/for_each.hpp with turning off specific
             compiler warnings
*/
// *****************************************************************************
#ifndef nowarning_for_each_h
#define nowarning_for_each_h

#include "Macro.h"

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wold-style-cast"
  #pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
  #pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif

#include <boost/mpl/for_each.hpp>

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

#endif // nowarning_for_each_h
