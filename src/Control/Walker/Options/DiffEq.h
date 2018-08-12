// *****************************************************************************
/*!
  \file      src/Control/Walker/Options/DiffEq.h
  \copyright 2012-2015, J. Bakosi, 2016-2018, Los Alamos National Security, LLC.
  \brief     Differential equation options and associations for walker
  \details   Differential equation options and associations for walker
*/
// *****************************************************************************
#ifndef WalkerDiffEqOptions_h
#define WalkerDiffEqOptions_h

#include <brigand/sequences/list.hpp>

#include "TaggedTuple.h"
#include "Toggle.h"
#include "Keywords.h"
#include "Walker/Options/InitPolicy.h"
#include "Walker/Options/CoeffPolicy.h"

namespace walker {
namespace ctr {

//! Differential equation types
enum class DiffEqType : uint8_t { NO_DIFFEQ=0,
                                  OU,
                                  DIAG_OU,
                                  SKEWNORMAL,
                                  GAMMA,
                                  BETA,
                                  NUMFRACBETA,
                                  MASSFRACBETA,
                                  MIXNUMFRACBETA,
                                  MIXMASSFRACBETA,
                                  DIRICHLET,
                                  GENDIR,
                                  WRIGHTFISHER,
                                  POSITION,
                                  DISSIPATION,
                                  VELOCITY };

//! Pack/Unpack: forward overload to generic enum class packer
inline void operator|( PUP::er& p, DiffEqType& e ) { PUP::pup( p, e ); }

//! Differential equation key used to access a diff eq in a factory
using DiffEqKey =
  tk::tuple::tagged_tuple< tag::diffeq,      DiffEqType,
                           tag::initpolicy,  ctr::InitPolicyType,
                           tag::coeffpolicy, ctr::CoeffPolicyType >;

//! Class with base templated on the above enum class with associations
class DiffEq : public tk::Toggle< DiffEqType > {

  public:
    // List valid expected choices to make them also available at compile-time
    using keywords = brigand::list< kw::ornstein_uhlenbeck
                                  , kw::diag_ou
                                  , kw::skewnormal
                                  , kw::gamma
                                  , kw::beta
                                  , kw::numfracbeta
                                  , kw::massfracbeta
                                  , kw::mixnumfracbeta
                                  , kw::mixmassfracbeta
                                  , kw::dirichlet
                                  , kw::gendir
                                  , kw::wrightfisher
                                  , kw::position
                                  , kw::dissipation
                                  , kw::velocity
                                  >;

    //! Constructor: pass associations references to base, which will handle
    //! class-user interactions
    explicit DiffEq() :
      tk::Toggle< DiffEqType >( "Differential equation",
        //! Enums -> names
        { { DiffEqType::NO_DIFFEQ, "n/a" },
          { DiffEqType::OU, kw::ornstein_uhlenbeck::name() },
          { DiffEqType::DIAG_OU, kw::diag_ou::name() },
          { DiffEqType::SKEWNORMAL, kw::skewnormal::name() },
          { DiffEqType::GAMMA, kw::gamma::name() },
          { DiffEqType::BETA, kw::beta::name() },
          { DiffEqType::NUMFRACBETA, kw::numfracbeta::name() },
          { DiffEqType::MASSFRACBETA, kw::massfracbeta::name() },
          { DiffEqType::MIXNUMFRACBETA, kw::mixnumfracbeta::name() },
          { DiffEqType::MIXMASSFRACBETA, kw::mixmassfracbeta::name() },
          { DiffEqType::DIRICHLET, kw::dirichlet::name() },
          { DiffEqType::GENDIR, kw::gendir::name() },
          { DiffEqType::WRIGHTFISHER, kw::wrightfisher::name() },
          { DiffEqType::POSITION, kw::position::name() },
          { DiffEqType::DISSIPATION, kw::dissipation::name() },
          { DiffEqType::VELOCITY, kw::velocity::name() } },
        //! keywords -> Enums
        { { "no_diffeq", DiffEqType::NO_DIFFEQ },
          { kw::ornstein_uhlenbeck::string(), DiffEqType::OU },
          { kw::diag_ou::string(), DiffEqType::DIAG_OU },
          { kw::skewnormal::string(), DiffEqType::SKEWNORMAL },
          { kw::gamma::string(), DiffEqType::GAMMA },
          { kw::beta::string(), DiffEqType::BETA },
          { kw::numfracbeta::string(), DiffEqType::NUMFRACBETA },
          { kw::massfracbeta::string(), DiffEqType::MASSFRACBETA },
          { kw::mixnumfracbeta::string(), DiffEqType::MIXNUMFRACBETA },
          { kw::mixmassfracbeta::string(), DiffEqType::MIXMASSFRACBETA },
          { kw::dirichlet::string(), DiffEqType::DIRICHLET },
          { kw::gendir::string(), DiffEqType::GENDIR },
          { kw::wrightfisher::string(), DiffEqType::WRIGHTFISHER },
          { kw::position::string(), DiffEqType::POSITION },
          { kw::dissipation::string(), DiffEqType::DISSIPATION },
          { kw::velocity::string(), DiffEqType::VELOCITY } } ) {}
};

} // ctr::
} // walker::

#endif // WalkerDiffEqOptions_h
