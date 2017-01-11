// *****************************************************************************
/*!
  \file      src/NoWarning/uniform.h
  \author    J. Bakosi
  \date      Tue 26 Jul 2016 07:45:56 AM MDT
  \copyright 2012-2015, Jozsef Bakosi, 2016, Los Alamos National Security, LLC.
  \brief     Include Random123/uniform.hpp with turning off specific compiler
             warnings
*/
// *****************************************************************************
#ifndef nowarning_uniform_h
#define nowarning_uniform_h

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wreserved-id-macro"
  #pragma clang diagnostic ignored "-Wundef"
#endif

#include <Random123/uniform.hpp>

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

#endif // nowarning_uniform_h
