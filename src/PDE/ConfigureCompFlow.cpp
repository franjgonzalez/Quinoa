// *****************************************************************************
/*!
  \file      src/PDE/ConfigureCompFlow.cpp
  \copyright 2012-2015 J. Bakosi,
             2016-2018 Los Alamos National Security, LLC.,
             2019 Triad National Security, LLC.
             All rights reserved. See the LICENSE file for details.
  \brief     Register and compile configuration for compressible flow PDE
  \details   Register and compile configuration for compressible flow PDE.
*/
// *****************************************************************************

#include <set>
#include <map>
#include <vector>
#include <string>

#include <brigand/algorithms/for_each.hpp>

#include "Tags.hpp"
#include "CartesianProduct.hpp"
#include "PDEFactory.hpp"
#include "Inciter/Options/PDE.hpp"

#include "ConfigureCompFlow.hpp"
#include "CompFlow/Physics/CG.hpp"
#include "CompFlow/Physics/DG.hpp"
#include "CompFlow/CGCompFlow.hpp"
#include "CompFlow/DGCompFlow.hpp"
#include "CompFlow/Problem.hpp"

namespace inciter {

void
registerCompFlow( CGFactory& cf,
                  DGFactory& df,
                  std::set< ctr::PDEType >& cgt,
                  std::set< ctr::PDEType >& dgt )
// *****************************************************************************
// Register compressible flow PDE into PDE factory
//! \param[in,out] cf Continuous Galerkin PDE factory to register to
//! \param[in,out] df Discontinuous Galerkin PDE factory to register to
//! \param[in,out] cgt Counters for equation types registered into CG factory
//! \param[in,out] dgt Counters for equation types registered into DG factory
// *****************************************************************************
{
  // Construct vector of vectors for all possible policies
  using CGCompFlowPolicies =
    tk::cartesian_product< cg::CompFlowPhysics, CompFlowProblems >;
  // Register PDEs for all combinations of policies
  brigand::for_each< CGCompFlowPolicies >(
    registerCG< cg::CompFlow >( cf, cgt, ctr::PDEType::COMPFLOW ) );

  // Construct vector of vectors for all possible policies
  using DGCompFlowPolicies =
    tk::cartesian_product< dg::CompFlowPhysics, CompFlowProblems >;
  // Register PDEs for all combinations of policies
  brigand::for_each< DGCompFlowPolicies >(
    registerDG< dg::CompFlow >( df, dgt, ctr::PDEType::COMPFLOW ) );
}

std::vector< std::pair< std::string, std::string > >
infoCompFlow( std::map< ctr::PDEType, tk::ctr::ncomp_type >& cnt )
// *****************************************************************************
//  Return information on the compressible flow system of PDEs
//! \param[inout] cnt std::map of counters for all PDE types
//! \return vector of string pairs describing the PDE configuration
// *****************************************************************************
{
  using eq = tag::compflow;

  auto c = ++cnt[ ctr::PDEType::COMPFLOW ];       // count eqs
  --c;  // used to index vectors starting with 0

  std::vector< std::pair< std::string, std::string > > nfo;

  nfo.emplace_back( ctr::PDE().name( ctr::PDEType::COMPFLOW ), "" );

  nfo.emplace_back( "dependent variable", std::string( 1,
    g_inputdeck.get< tag::param, eq, tag::depvar >()[c] ) );

  nfo.emplace_back( "physics", ctr::Physics().name(
    g_inputdeck.get< tag::param, eq, tag::physics >()[c] ) );

  nfo.emplace_back( "problem", ctr::Problem().name(
    g_inputdeck.get< tag::param, eq, tag::problem >()[c] ) );

  auto ncomp = g_inputdeck.get< tag::component >().get< eq >()[c];
  nfo.emplace_back( "number of components", std::to_string( ncomp ) );

  nfo.emplace_back( "start offset in unknowns array", std::to_string(
    g_inputdeck.get< tag::component >().offset< eq >(c) ) );

  const auto& gamma = g_inputdeck.get< tag::param, eq, tag::gamma >()[c];
  if (!gamma.empty())
    nfo.emplace_back( "ratio of specific heats", std::to_string( gamma[0] )) ;

  const auto& pstiff = g_inputdeck.get< tag::param, eq, tag::pstiff >()[c];
  if (!pstiff.empty())
    nfo.emplace_back( "material stiffness", std::to_string( pstiff[0] ) );

  // Viscosity is optional: the outer vector may be empty
  const auto& mu = g_inputdeck.get< tag::param, eq, tag::mu >();
  if (mu.size() > c)
    nfo.emplace_back( "dynamic viscosity", std::to_string( mu[c][0] ) );

  const auto& cv = g_inputdeck.get< tag::param, eq, tag::cv >()[c];
  if (!cv.empty())
    nfo.emplace_back( "specific heat at const. volume", std::to_string(cv[0]) );

  // Heat conductivity is optional: the outer vector may be empty
  const auto& k = g_inputdeck.get< tag::param, eq, tag::k >();
  if (k.size() > c)
    nfo.emplace_back( "heat conductivity", std::to_string( k[c][0] ) );

  const auto& npar = g_inputdeck.get< tag::param, eq, tag::npar >();
  if (!npar.empty())
    nfo.emplace_back( "number of tracker particles", parameters( npar ) );

  const auto& alpha = g_inputdeck.get< tag::param, eq, tag::alpha >();
  if (!alpha.empty()) nfo.emplace_back( "coeff alpha", parameters( alpha ) );

  const auto& beta =
    g_inputdeck.get< tag::param, eq, tag::beta >();
  if (!beta.empty())
    nfo.emplace_back( "coeff beta", parameters( beta ) );

  const auto& bx = g_inputdeck.get< tag::param, eq, tag::betax >();
  if (!bx.empty()) nfo.emplace_back( "coeff betax", parameters( bx ) );

  const auto& by = g_inputdeck.get< tag::param, eq, tag::betay >();
  if (!by.empty()) nfo.emplace_back( "coeff betay", parameters( by ) );

  const auto& bz = g_inputdeck.get< tag::param, eq, tag::betaz >();
  if (!bz.empty()) nfo.emplace_back( "coeff betaz", parameters( bz ) );

  const auto& r0 = g_inputdeck.get< tag::param, eq, tag::r0 >();
  if (!r0.empty()) nfo.emplace_back( "coeff r0", parameters( r0 ) );

  const auto& ce = g_inputdeck.get< tag::param, eq, tag::ce >();
  if (!ce.empty()) nfo.emplace_back( "coeff ce", parameters( ce ) );

  const auto& kappa = g_inputdeck.get< tag::param, eq, tag::kappa >();
  if (!kappa.empty()) nfo.emplace_back( "coeff k", parameters( kappa ) );

  const auto& p0 = g_inputdeck.get< tag::param, eq, tag::p0 >();
  if (!p0.empty()) nfo.emplace_back( "coeff p0", parameters( p0 ) );

  return nfo;
}

}  // inciter::
