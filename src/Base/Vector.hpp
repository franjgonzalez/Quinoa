// *****************************************************************************
/*!
  \file      src/Base/Vector.hpp
  \copyright 2012-2015 J. Bakosi,
             2016-2018 Los Alamos National Security, LLC.,
             2019 Triad National Security, LLC.
             All rights reserved. See the LICENSE file for details.
  \brief     Vector algebra
  \details   Vector algebra.
*/
// *****************************************************************************
#ifndef Vector_h
#define Vector_h

#include <array>

#include "Types.hpp"

namespace tk {

//! Compute the cross-product of two vectors
std::array< real, 3 >
cross( const std::array< real, 3 >& v1, const std::array< real, 3 >& v2 );

//! Compute the cross-product of two vectors divided by a scalar
std::array< real, 3 >
crossdiv( const std::array< real, 3 >& v1,
          const std::array< real, 3 >& v2,
          real j );

//! Compute the dot-product of two vectors
real
dot( const std::array< real, 3 >& v1, const std::array< real, 3 >& v2 );

//! Compute the triple-product of three vectors
real
triple( const std::array< real, 3 >& v1,
        const std::array< real, 3 >& v2,
        const std::array< real, 3 >& v3 );

//! Rotate vector about X axis
std::array< real, 3 >
rotateX( const std::array< real, 3 >& v, real angle );

//! Rotate vector about Y axis
std::array< real, 3 >
rotateY( const std::array< real, 3 >& v, real angle );

//! Rotate vector about Z axis
std::array< real, 3 >
rotateZ( const std::array< real, 3 >& v, real angle );

//! \brief Compute the determinant of the Jacobian of a coordinate
//!  transformation over a tetrahedron
real
Jacobian( const std::array< real, 3 >& v1,
          const std::array< real, 3 >& v2,
          const std::array< real, 3 >& v3,
          const std::array< real, 3 >& v4 );

//! \brief Compute the inverse of the Jacobian of a coordinate transformation
//!   over a tetrahedron
std::array< std::array< real, 3 >, 3 >
inverseJacobian( const std::array< real, 3 >& v1,
                 const std::array< real, 3 >& v2,
                 const std::array< real, 3 >& v3,
                 const std::array< real, 3 >& v4 );

} // tk::

#endif // Vector_h