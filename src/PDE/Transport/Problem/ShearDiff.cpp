// *****************************************************************************
/*!
  \file      src/PDE/Transport/Problem/ShearDiff.cpp
  \copyright 2012-2015 J. Bakosi,
             2016-2018 Los Alamos National Security, LLC.,
             2019 Triad National Security, LLC.
             All rights reserved. See the LICENSE file for details.
  \brief     Problem configuration for transport equations
  \details   This file defines a Problem policy class for the transport
    equations, defined in PDE/Transport/CGTransport.h implementing
    node-centered continuous Galerkin (CG) and PDE/Transport/DGTransport.h
    implementing cell-centered discontinuous Galerkin (DG) discretizations.
    See PDE/Transport/Problem.h for general requirements on Problem policy
    classes for cg::Transport and dg::Transport.
*/
// *****************************************************************************

#include "ShearDiff.hpp"
#include "Inciter/InputDeck/InputDeck.hpp"

namespace inciter {

extern ctr::InputDeck g_inputdeck;

} // ::inciter

using inciter::TransportProblemShearDiff;

std::vector< tk::real >
TransportProblemShearDiff::solution( ncomp_t system, ncomp_t ncomp,
          tk::real x, tk::real y, tk::real z, tk::real t )
// *****************************************************************************
//  Evaluate analytical solution at (x,y,z,t) for all components
//! \param[in] system Equation system index
//! \param[in] ncomp Number of components in this transport equation system
//! \param[in] x X coordinate where to evaluate the solution
//! \param[in] y Y coordinate where to evaluate the solution
//! \param[in] z Z coordinate where to evaluate the solution
//! \param[in] t Time where to evaluate the solution
//! \return Values of all components evaluated at (x,y,t)
// *****************************************************************************
{
  using tag::param;

  const auto& u0 = g_inputdeck.get< param, eq, tag::u0 >()[system];
  const auto& d = g_inputdeck.get< param, eq, tag::diffusivity >()[system];
  const auto& l = g_inputdeck.get< param, eq, tag::lambda >()[system];

  std::vector< tk::real > r( ncomp );
  for (ncomp_t c=0; c<ncomp; ++c) {
    const auto li = 2*c;
    const auto di = 3*c;
    const auto phi3s = (l[li+0]*l[li+0]*d[di+1]/d[di+0] +
                        l[li+1]*l[li+1]*d[di+2]/d[di+0]) / 12.0;
    r[c] =
        1.0 / ( 8.0 * std::pow(M_PI,3.0/2.0) *
                std::sqrt(d[di+0]*d[di+1]*d[di+2]) *
                std::pow(t,3.0/2.0) * std::sqrt(1.0+phi3s*t*t) ) *
        exp( -std::pow( x - u0[c]*t -
                        0.5*(l[li+0]*y + l[li+1]*z)*t, 2.0 ) /
              ( 4.0 * d[di+0] * t * (1.0 + phi3s*t*t) )
             -y*y / ( 4.0 * d[di+1] * t )
             -z*z / ( 4.0 * d[di+2] * t ) );
  }

  return r;
}

std::vector< tk::real >
TransportProblemShearDiff::solinc( ncomp_t system, ncomp_t ncomp, tk::real x,
                              tk::real y, tk::real z, tk::real t, tk::real dt )
const
// *****************************************************************************
//  Evaluate the increment from t to t+dt of the analytical solution at (x,y,z)
//  for all components
//! \param[in] system Equation system index
//! \param[in] ncomp Number of components in this transport equation system
//! \param[in] x X coordinate where to evaluate the solution
//! \param[in] y Y coordinate where to evaluate the solution
//! \param[in] z Z coordinate where to evaluate the solution
//! \param[in] t Time where to evaluate the solution increment starting from
//! \param[in] dt Time increment at which evaluate the solution increment to
//! \return Increment in values of all components evaluated at (x,y,t+dt)
// *****************************************************************************
{
  auto st1 = solution( system, ncomp, x, y, z, t );
  auto st2 = solution( system, ncomp, x, y, z, t+dt );

  std::transform( begin(st1), end(st1), begin(st2), begin(st2),
                  []( tk::real s, tk::real& d ){ return d -= s; } );

  return st2;
}

void
TransportProblemShearDiff::errchk( ncomp_t system, ncomp_t ncomp ) const
// *****************************************************************************
//  Do error checking on PDE parameters
//! \param[in] system Equation system index, i.e., which transport equation
//!   system we operate on among the systems of PDEs
//! \param[in] ncomp Number of components in this transport equation
// *****************************************************************************
{
  using tag::param;

  const auto& u0 = g_inputdeck.get< param, eq, tag::u0 >()[system];
  ErrChk( ncomp == u0.size(),
    "Wrong number of advection-diffusion PDE parameters 'u0'" );

  const auto& lambda = g_inputdeck.get< param, eq, tag::lambda >()[system];
  ErrChk( 2*ncomp == lambda.size(),
    "Wrong number of advection-diffusion PDE parameters 'lambda'" );

  const auto& d = g_inputdeck.get< param, eq, tag::diffusivity >()[system];
  ErrChk( 3*ncomp == d.size(),
    "Wrong number of advection-diffusion PDE parameters 'diffusivity'" );
}

void
TransportProblemShearDiff::side( std::unordered_set< int >& conf ) const
// *****************************************************************************
//  Query all side set IDs the user has configured for all components in this
//  PDE system
//! \param[in,out] conf Set of unique side set IDs to add to
// *****************************************************************************
{
  using tag::param; using tag::bcdir;

  for (const auto& s : g_inputdeck.get< param, eq, bcdir >())
    for (const auto& i : s)
      conf.insert( std::stoi(i) );
}

std::vector< std::array< tk::real, 3 > >
TransportProblemShearDiff::prescribedVelocity( ncomp_t system, ncomp_t ncomp,
                                              tk::real, tk::real y, tk::real z )
// *****************************************************************************
//  Assign prescribed shear velocity at a point
//! \param[in] system Equation system index, i.e., which transport equation
//!   system we operate on among the systems of PDEs
//! \param[in] ncomp Number of components in this transport equation
//! \param[in] y y coordinate at which to assign velocity
//! \param[in] z Z coordinate at which to assign velocity
//! \return Velocity assigned to all vertices of a tetrehedron, size:
//!   ncomp * ndim = [ncomp][3]
// *****************************************************************************
{
  using tag::param;

  const auto& u0 = g_inputdeck.get< param, eq, tag::u0 >()[ system ];
  const auto& l = g_inputdeck.get< param, eq, tag::lambda >()[ system ];

  std::vector< std::array< tk::real, 3 > > vel( ncomp );
  for (ncomp_t c=0; c<ncomp; ++c)
    vel[c] = {{ u0[c] + l[2*c+0]*y + l[2*c+1]*z, 0.0, 0.0 }};

  return vel;
}
