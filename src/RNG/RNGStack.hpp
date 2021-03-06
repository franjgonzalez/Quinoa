// *****************************************************************************
/*!
  \file      src/RNG/RNGStack.hpp
  \copyright 2012-2015 J. Bakosi,
             2016-2018 Los Alamos National Security, LLC.,
             2019 Triad National Security, LLC.
             All rights reserved. See the LICENSE file for details.
  \brief     Stack of random number generators
  \details   This file declares class RNGStack, which implements various
    functionality related to registering and instantiating random number
    generators interfacing to multiple RNG libraries. Registration and
    instantiation use a random number generator factory, which is a std::map (an
    associative container), associating unique RNG keys to their constructor
    calls. For more details, see the in-code documentation of the constructor.
*/
// *****************************************************************************
#ifndef RNGStack_h
#define RNGStack_h

#include <map>
#include <vector>
#include <functional>
#include <type_traits>

#include "RNG.hpp"
#include "RNGParam.hpp"
#include "Options/RNG.hpp"
#include "QuinoaConfig.hpp"

namespace tk {

//! Random number generator factory: keys associated to their constructors
using RNGFactory = std::map< ctr::RNGType, std::function< RNG() > >;

//! \brief Random number generator stack
class RNGStack {

  public:
    //! Constructor: register random number generators into factory
    explicit RNGStack(
                       #ifdef HAS_MKL
                       const ctr::RNGMKLParameters& mklparam,
                       #endif
                       #ifdef HAS_RNGSSE2
                       const ctr::RNGSSEParameters& rngsseparam,
                       #endif
                       const ctr::RNGRandom123Parameters& r123param );

    //! Instantiate selected RNGs
    std::map< std::underlying_type< tk::ctr::RNGType >::type, tk::RNG >
    selected( const std::vector< ctr::RNGType >& sel ) const;

    //! Instantiate a RNG
    tk::RNG create( tk::ctr::RNGType r ) const;

  private:
   #ifdef HAS_MKL
   //! Register MKL RNGs into factory
   void regMKL( int nstream, const ctr::RNGMKLParameters& param );
   #endif

   #ifdef HAS_RNGSSE2
   //! Register RNGSSE RNGs into factory
   void regRNGSSE( int nstream, const ctr::RNGSSEParameters& param );
   #endif

   //! Register Random123 RNGs into factory
   void regRandom123( int nstream, const ctr::RNGRandom123Parameters& param );

   RNGFactory m_factory;        //!< Random nunmber generator factory
};

} // namespace tk

#endif // RNGStack_h
