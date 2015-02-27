//******************************************************************************
/*!
  \file      src/Control/Walker/InputDeck/Grammar.h
  \author    J. Bakosi
  \date      Fri 27 Feb 2015 08:32:24 AM MST
  \copyright 2012-2015, Jozsef Bakosi.
  \brief     Walker's input deck grammar definition
  \details   Walker's input deck grammar definition. We use the [Parsing
    Expression Grammar Template Library (PEGTL)]
    (https://code.google.com/p/pegtl/wiki/PEGTL0) to create the grammar and the
    associated parser. Credit goes to Colin Hirsch (pegtl@cohi.at) for PEGTL.
    Word of advice: read from the bottom up.
*/
//******************************************************************************
#ifndef WalkerInputDeckGrammar_h
#define WalkerInputDeckGrammar_h

#include <Macro.h>
#include <Exception.h>
#include <PEGTLParsed.h>
#include <Walker/Types.h>
#include <Keywords.h>
#include <Grammar.h>
#include <Options/InitPolicy.h>

#ifdef HAS_MKL
#include <MKLGrammar.h>
#endif

#include <RNGSSEGrammar.h>

namespace walker {

extern ctr::InputDeck g_inputdeck_defaults;

//! Walker input deck facilitating user input for integrating SDEs
namespace deck {

  //! PEGTLParsed type specialized to Walker's input deck parser
  using PEGTLInputDeck =
    tk::ctr::PEGTLParsed< ctr::InputDeck,
                          pegtl::file_input< ctr::Location >,
                          tag::cmd,
                          ctr::CmdLine >;

  //! \brief Specialization of tk::grm::use for Walker's input deck parser
  //! \author J. Bakosi
  template< typename keyword >
  using use = tk::grm::use< keyword,
                            ctr::InputDeck::keywords1,
                            ctr::InputDeck::keywords2,
                            ctr::InputDeck::keywords3,
                            ctr::InputDeck::keywords4,
                            ctr::InputDeck::keywords5,
                            ctr::InputDeck::keywords6 >;

  // Walker's InputDeck state

  //! Everything is stored in Stack during parsing
  using Stack = PEGTLInputDeck;

  //! \brief Number of registered equations
  //! \details Counts the number of parsed equation blocks during parsing.
  //! \author J. Bakosi
  tk::tuple::tagged_tuple< tag::dirichlet,    std::size_t,
                           tag::gendir,       std::size_t,
                           tag::wrightfisher, std::size_t,
                           tag::ou,           std::size_t,
                           tag::diagou,       std::size_t,
                           tag::skewnormal,   std::size_t,
                           tag::gamma,        std::size_t,
                           tag::beta,         std::size_t,
                           tag::nfracbeta,    std::size_t > neq;

  // Walker's InputDeck actions

  //! \brief Put option in state at position given by tags
  //! \details This is simply a wrapper around tk::grm::store_option passing the
  //!    stack defaults.
  //! \author J. Bakosi
  template< class Option, typename... tags >
  struct store_option : pegtl::action_base< store_option< Option, tags... > > {
    static void apply(const std::string& value, Stack& stack) {
      tk::grm::store_option< Stack, use, Option, ctr::InputDeck, tags... >
                           ( stack, value, g_inputdeck_defaults );
    }
  };

  //! \brief Register differential equation after parsing its block
  //! \details This is used by the error checking functors (check_*) during
  //!    parsing to identify the recently-parsed block.
  //! \author J. Bakosi
  template< class eq >
  struct register_eq : pegtl::action_base< register_eq< eq > > {
    static void apply( const std::string& value, Stack& stack ) {
      ++neq.get< eq >();
    }
  };

  //! \brief Do general error checking on the differential equation block
  //! \details This is error checking that all equation types must satisfy.
  //! \author J. Bakosi
  template< class eq >
  struct check_eq : pegtl::action_base< check_eq< eq > > {
    static void apply( const std::string& value, Stack& stack ) {

      // Error out if no dependent variable has been selected
      const auto& depvar = stack.get< tag::param, eq, tag::depvar >();
      if (depvar.empty() || depvar.size() != neq.get< eq >())
        tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NODEPVAR >
                        ( stack, value );

      // Error out if no number of components has been selected
      const auto& ncomp = stack.get< tag::component, eq >();
      if (ncomp.empty() || ncomp.size() != neq.get< eq >())
        tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NONCOMP >
                        ( stack, value );

      // Error out if no RNG has been selected
      const auto& rng = stack.get< tag::param, eq, tag::rng >();
      if (rng.empty() || rng.size() != neq.get< eq >())
        tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NORNG >
                        ( stack, value );

      // Error out if no initialization policy has been selected
      const auto& init = stack.get< tag::param, eq, tag::initpolicy >();
      if (init.empty() || init.size() != neq.get< eq >())
        tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NOINIT >
                        ( stack, value );

      // Error out if no coefficients policy has been selected
      const auto& coeff = stack.get< tag::param, eq, tag::coeffpolicy >();
      if (coeff.empty() || coeff.size() != neq.get< eq >())
        tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NOCOEFF >
                        ( stack, value );
    }
  };

  //! \brief Do error checking on the selected initialization policy
  //! \author J. Bakosi
  template< class eq >
  struct check_init : pegtl::action_base< check_init< eq > > {
    static void apply( const std::string& value, Stack& stack ) {

      // Error checks for delta initpolicy
      const auto& init = stack.get< tag::param, eq, tag::initpolicy >();
      if (init.size() == neq.get< eq >() &&
          init.back() == tk::ctr::InitPolicyType::DELTA) {
        // Make sure there was a delta...end block with at least a single
        // spike...end block
        const auto& spike = stack.template get< tag::param, eq, tag::spike >();
        if (!spike.empty() && spike.back().empty())
          tk::grm::Message< Stack, tk::grm::ERROR, tk::grm::MsgKey::NODELTA >
                          ( stack, value );
      }
    }
  };


  // Walker's InputDeck grammar

  //! scan and store_back sde keyword and option
  template< typename keyword, class eq >
  struct scan_sde :
         tk::grm::scan< Stack,
                        typename keyword::pegtl_string,
                        tk::grm::store_back_option< Stack,
                                                    use,
                                                    ctr::DiffEq,
                                                    tag::selected,
                                                    tag::diffeq >,
                        // start new vector or vectors of spikes for a potential
                        // delta initpolicy
                        tk::grm::start_vector< Stack,
                                               tag::param,
                                               eq,
                                               tag::spike > > {};

  //! Discretization parameters
  struct discretization_parameters :
         pegtl::sor< tk::grm::discr< Stack, use< kw::npar >, tag::npar >,
                     tk::grm::discr< Stack, use< kw::nstep >, tag::nstep >,
                     tk::grm::discr< Stack, use< kw::term >, tag::term >,
                     tk::grm::discr< Stack, use< kw::dt >, tag::dt >,
                     tk::grm::interval< Stack, use< kw::ttyi >, tag::tty > > {};

  //! rngs
  struct rngs :
         pegtl::sor<
                     #ifdef HAS_MKL
                     tk::mkl::rngs< Stack, use,
                                    tag::selected, tag::rng,
                                    tag::param, tag::rngmkl >,
                     #endif
                     tk::rngsse::rngs< Stack, use,
                                       tag::selected, tag::rng,
                                       tag::param, tag::rngsse > > {};

  //! scan delta ... end block
  template< class eq >
  struct delta :
         pegtl::ifmust<
           tk::grm::readkw< use< kw::delta >::pegtl_string >,
           // parse a spike ... end block (there can be multiple)
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::spike >,
                             tk::grm::Store_back_back_back,
                             tk::grm::start_vector_back,
                             tk::grm::check_spikes,
                             eq,
                             tag::spike > > > {};

  //! Error checks after a equation..end block has been parsed
  template< class eq >
  struct check_errors :
         pegtl::seq<
           // register differential equation block
           pegtl::apply< register_eq< eq > >,
           // do error checking on this block
           pegtl::apply< check_eq< eq > >,
           // do error checking on the init policy
           pegtl::apply< check_init< eq > > > {};

  //! Diagonal Ornstein-Uhlenbeck SDE
  struct diag_ou :
         pegtl::ifmust<
           scan_sde< use< kw::diag_ou >, tag::diagou >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::diagou,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::diagou >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::diagou,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::diagou,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::diagou,
                                            tag::coeffpolicy >,
                           delta< tag::diagou >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_sigmasq >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::diagou,
                             tag::sigmasq >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_theta >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::diagou,
                             tag::theta >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_mu >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::diagou,
                             tag::mu > >,
           check_errors< tag::diagou > > {};

  //! Ornstein-Uhlenbeck SDE
  struct ornstein_uhlenbeck :
         pegtl::ifmust<
           scan_sde< use< kw::ornstein_uhlenbeck >, tag::ou >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::ou,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::ou >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::ou,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::ou,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::ou,
                                            tag::coeffpolicy >,
                           delta< tag::ou >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_sigmasq >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::ou,
                             tag::sigmasq >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_theta >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::ou,
                             tag::theta >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_mu >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::ou,
                             tag::mu > >,
           check_errors< tag::ou > > {};

  //! Skew-normal SDE
  struct skewnormal :
         pegtl::ifmust<
           scan_sde< use< kw::skewnormal >, tag::skewnormal >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::skewnormal,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::skewnormal >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::skewnormal,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::skewnormal,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::skewnormal,
                                            tag::coeffpolicy >,
                           delta< tag::skewnormal >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_T >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::skewnormal,
                             tag::timescale >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_sigmasq >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::skewnormal,
                             tag::sigmasq >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_lambda >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::skewnormal,
                             tag::lambda > >,
           check_errors< tag::skewnormal > > {};

  //! Beta SDE
  struct beta :
         pegtl::ifmust<
           scan_sde< use< kw::beta >, tag::beta >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::beta,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::beta >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::beta,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::beta,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::beta,
                                            tag::coeffpolicy >,
                           delta< tag::beta >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_b >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::beta,
                             tag::b >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_S >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::beta,
                             tag::S >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_kappa >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::beta,
                             tag::kappa > >,
           check_errors< tag::beta > > {};

  //! Number-fraction beta SDE
  struct nfracbeta :
         pegtl::ifmust<
           scan_sde< use< kw::nfracbeta >, tag::nfracbeta >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::nfracbeta,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::nfracbeta >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::nfracbeta,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::nfracbeta,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::nfracbeta,
                                            tag::coeffpolicy >,
                           delta< tag::nfracbeta >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_b >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::nfracbeta,
                             tag::b >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_S >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::nfracbeta,
                             tag::S >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_kappa >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::nfracbeta,
                             tag::kappa >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_rho2 >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::nfracbeta,
                             tag::rho2 >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_rcomma >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::nfracbeta,
                             tag::rcomma > >,
           check_errors< tag::nfracbeta > > {};

  //! Gamma SDE
  struct gamma :
         pegtl::ifmust<
           scan_sde< use< kw::gamma >, tag::gamma >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::gamma,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::gamma >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::gamma,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::gamma,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::gamma,
                                            tag::coeffpolicy >,
                           delta< tag::gamma >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_b >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gamma,
                             tag::b >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_S >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gamma,
                             tag::S >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_kappa >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gamma,
                             tag::kappa > >,
           check_errors< tag::gamma > > {};

  //! Dirichlet SDE
  struct dirichlet :
         pegtl::ifmust<
           scan_sde< use< kw::dirichlet >, tag::dirichlet >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::dirichlet,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::dirichlet >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::dirichlet,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::dirichlet,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::dirichlet,
                                            tag::coeffpolicy >,
                            delta< tag::dirichlet >,
                            tk::grm::parameter_vector<
                              Stack,
                              use,
                              use< kw::sde_b >,
                              tk::grm::Store_back_back,
                              tk::grm::start_vector,
                              tk::grm::check_vector,
                              tag::dirichlet,
                              tag::b >,
                            tk::grm::parameter_vector<
                              Stack,
                              use,
                              use< kw::sde_S >,
                              tk::grm::Store_back_back,
                              tk::grm::start_vector,
                              tk::grm::check_vector,
                              tag::dirichlet,
                              tag::S >,
                            tk::grm::parameter_vector<
                              Stack,
                              use,
                              use< kw::sde_kappa >,
                              tk::grm::Store_back_back,
                              tk::grm::start_vector,
                              tk::grm::check_vector,
                              tag::dirichlet,
                              tag::kappa > >,
           check_errors< tag::dirichlet > > {};

  //! Generalized Dirichlet SDE
  struct gendir :
         pegtl::ifmust<
           scan_sde< use< kw::gendir >, tag::gendir >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::gendir,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::gendir >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::gendir,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::gendir,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::gendir,
                                            tag::coeffpolicy >,
                           delta< tag::gendir >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_b >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gendir,
                             tag::b >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_S >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gendir,
                             tag::S >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_kappa >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gendir,
                             tag::kappa >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_c >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::gendir,
                             tag::c > >,
           check_errors< tag::gendir > > {};

  //! Wright-Fisher SDE
  struct wright_fisher :
         pegtl::ifmust<
           scan_sde< use< kw::wrightfisher >, tag::wrightfisher >,
           tk::grm::block< Stack,
                           use< kw::end >,
                           tk::grm::depvar< Stack,
                                            use,
                                            tag::wrightfisher,
                                            tag::depvar >,
                           tk::grm::component< Stack,
                                               use< kw::ncomp >,
                                               tag::wrightfisher >,
                           tk::grm::rng< Stack,
                                         use,
                                         use< kw::rng >,
                                         tk::ctr::RNG,
                                         tag::wrightfisher,
                                         tag::rng >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::init >,
                                            tk::ctr::InitPolicy,
                                            tag::wrightfisher,
                                            tag::initpolicy >,
                           tk::grm::policy< Stack,
                                            use,
                                            use< kw::coeff >,
                                            tk::ctr::CoeffPolicy,
                                            tag::wrightfisher,
                                            tag::coeffpolicy >,
                           delta< tag::wrightfisher >,
                           tk::grm::parameter_vector<
                             Stack,
                             use,
                             use< kw::sde_omega >,
                             tk::grm::Store_back_back,
                             tk::grm::start_vector,
                             tk::grm::check_vector,
                             tag::wrightfisher,
                             tag::omega > >,
           check_errors< tag::wrightfisher > > {};

  //! stochastic differential equations
  struct sde :
         pegtl::sor< dirichlet,
                     gendir,
                     wright_fisher,
                     ornstein_uhlenbeck,
                     diag_ou,
                     skewnormal,
                     gamma,
                     beta,
                     nfracbeta > {};

  //! 'walker' block
  struct walker :
         pegtl::ifmust<
           tk::grm::readkw< use< kw::walker >::pegtl_string >,
           pegtl::sor< tk::grm::block< Stack,
                         use< kw::end >,
                         discretization_parameters,
                         sde,
                         tk::grm::rngblock< Stack, use, rngs >,
                         tk::grm::statistics< Stack, use >,
                         tk::grm::pdfs< Stack, use, store_option > >,
                       pegtl::apply<
                          tk::grm::error< Stack,
                                          tk::grm::MsgKey::UNFINISHED > > > > {};

  //! main keywords
  struct keywords :
         pegtl::sor< tk::grm::title< Stack, use >, walker > {};

  //! entry point: parse keywords and ignores until eof
  struct read_file :
         tk::grm::read_file< Stack, keywords, tk::grm::ignore > {};

} // deck::
} // walker::

#endif // WalkerInputDeckGrammar_h
