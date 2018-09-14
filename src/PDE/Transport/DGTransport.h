// *****************************************************************************
/*!
  \file      src/PDE/Transport/DGTransport.h
  \copyright 2012-2015, J. Bakosi, 2016-2018, Los Alamos National Security, LLC.
  \brief     Scalar transport using disccontinous Galerkin discretization
  \details   This file implements the physics operators governing transported
     scalars using disccontinuous Galerkin discretization.
*/
// *****************************************************************************
#ifndef DGTransport_h
#define DGTransport_h

#include <vector>
#include <array>
#include <limits>
#include <cmath>
#include <unordered_set>
#include <map>

#include "Macro.h"
#include "Exception.h"
#include "Vector.h"
#include "Inciter/Options/BC.h"
#include "UnsMesh.h"

namespace inciter {

extern ctr::InputDeck g_inputdeck;

namespace dg {

//! \brief Transport equation used polymorphically with tk::DGPDE
//! \details The template argument(s) specify policies and are used to configure
//!   the behavior of the class. The policies are:
//!   - Physics - physics configuration, see PDE/Transport/Physics.h
//!   - Problem - problem configuration, see PDE/Transport/Problem.h
//! \note The default physics is DGAdvection, set in
//!    inciter::deck::check_transport()
template< class Physics, class Problem >
class Transport {

  private:
    using ncomp_t = kw::ncomp::info::expect::type;
    using bcconf_t = kw::sideset::info::expect::type;

    //! Extract BC configuration ignoring if BC not specified
    //! \param[in] c Equation system index (among multiple systems configured)
    //! \return Vector of BC config of type bcconf_t used to apply BCs for all
    //!   scalar components this Transport eq system is configured for
    //! \note A more preferable way of catching errors such as this function
    //!   hides is during parsing, so that we don't even get here if BCs are not
    //!   correctly specified. For now we simply ignore if BCs are not
    //!   specified by allowing empty BC vectors from the user input.
    template< typename bctag >
    std::vector< bcconf_t >
    config( ncomp_t c ) {
      std::vector< bcconf_t > bc;
      const auto& v = g_inputdeck.get< tag::param, tag::transport, bctag >();
      if (v.size() > c) bc = v[c];
      return bc;
    }

  public:
    //! Constructor
    //! \param[in] c Equation system index (among multiple systems configured)
    explicit Transport( ncomp_t c ) :
      m_c( c ),
      m_ncomp(
        g_inputdeck.get< tag::component >().get< tag::transport >().at(c) ),
      m_offset(
        g_inputdeck.get< tag::component >().offset< tag::transport >(c) ),
      m_bcextrapolate( config< tag::bcextrapolate >( c ) ),
      m_bcinlet( config< tag::bcinlet >( c ) ),
      m_bcoutlet( config< tag::bcoutlet >( c ) ),
      m_ndof( 4 )
    {
      Problem::errchk( m_c, m_ncomp );
    }

    //! Initalize the transport equations using problem policy
    //! \param[in] geoElem Element geometry array
    //! \param[in,out] unk Array of unknowns
    //! \param[in] t Physical time
    void initialize( const tk::Fields& geoElem,
                     tk::Fields& unk,
                     tk::real t ) const
    {
      Assert( geoElem.nunk() == unk.nunk(), "Size mismatch" );
      std::size_t nelem = unk.nunk();

      for (std::size_t e=0; e<nelem; ++e)
      {
        auto xcc = geoElem(e,1,0);
        auto ycc = geoElem(e,2,0);
        auto zcc = geoElem(e,3,0);

        const auto s = Problem::solution( m_c, m_ncomp, xcc, ycc, zcc, t );
        for (ncomp_t c=0; c<m_ncomp; ++c)
          unk(e, c, m_offset) = s[c];
      }
    }

    //! Compute the left hand side mass matrix
    //! \param[in] geoElem Element geometry array
    //! \param[in,out] l Block diagonal mass matrix
    void lhs( const tk::Fields& geoElem, tk::Fields& l ) const
    {
      Assert( geoElem.nunk() == l.nunk(), "Size mismatch" );
      std::size_t nelem = geoElem.nunk();

      for (std::size_t e=0; e<nelem; ++e)
      {
        for (ncomp_t c=0; c<m_ncomp; ++c)
          l(e, c, m_offset) = geoElem(e,0,0);
      }
    }

    //! Compute the left hand side P1 mass matrix
    //! \param[in] geoElem Element geometry array
    //! \param[in,out] l Block diagonal mass matrix
    void lhsp1( const tk::Fields& geoElem, tk::Fields& l ) const
    {
      Assert( geoElem.nunk() == l.nunk(), "Size mismatch" );
      std::size_t nelem = geoElem.nunk();

      for (std::size_t e=0; e<nelem; ++e)
      {
        for (ncomp_t c=0; c<m_ncomp; ++c)
        {
          auto mark = c*m_ndof;
          l(e, mark,   m_offset) = geoElem(e,0,0);
          l(e, mark+1, m_offset) = geoElem(e,0,0) / 10.0;
          l(e, mark+2, m_offset) = geoElem(e,0,0) * 3.0/10.0;
          l(e, mark+3, m_offset) = geoElem(e,0,0) * 3.0/5.0;
        }
      }
    }

    //! Compute right hand side
    //! \param[in] geoFace Face geometry array
    //! \param[in] fd Face connectivity and boundary conditions object
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void rhs( tk::real,
              const tk::Fields& geoFace,
              const tk::Fields&,
              const inciter::FaceData& fd,
              const tk::Fields& U,
              tk::Fields& R ) const
    {
      Assert( U.nunk() == R.nunk(), "Number of unknowns in solution "
              "vector and right-hand side at recent time step incorrect" );
      Assert( U.nprop() == m_ncomp && R.nprop() == m_ncomp,
              "Number of components in solution and right-hand side vector " 
              "must equal "+ std::to_string(m_ncomp) );

      const auto& bface = fd.Bface();
      const auto& esuf = fd.Esuf();

      // set rhs to zero
      R.fill(0.0);

      // compute internal surface flux integrals
      for (auto f=fd.Nbfac(); f<esuf.size()/2; ++f)
      {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        std::size_t er = static_cast< std::size_t >(esuf[2*f+1]);
        auto farea = geoFace(f,0,0);

        //--- upwind fluxes
        auto flux = upwindFlux( {{geoFace(f,4,0), geoFace(f,5,0), geoFace(f,6,0)}},
                                f, geoFace, {{U.extract(el), U.extract(er)}} );

        for (ncomp_t c=0; c<m_ncomp; ++c) {
          R(el, c, m_offset) -= farea * flux[c];
          R(er, c, m_offset) += farea * flux[c];
        }
      }

      // compute boundary surface flux integrals
      bndIntegral< Extrapolate >( m_bcextrapolate, bface, esuf, geoFace, U, R );
      bndIntegral< Inlet >( m_bcinlet, bface, esuf, geoFace, U, R );
      bndIntegral< Outlet >( m_bcoutlet, bface, esuf, geoFace, U, R );
    }

    //! Compute P1 right hand side
    //! \param[in] geoFace Face geometry array
    //! \param[in] geoElem Element geometry array
    //! \param[in] fd Face connectivity and boundary conditions object
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void rhsp1( tk::real,
                const tk::Fields& geoFace,
                const tk::Fields& geoElem,
                const inciter::FaceData& fd,
                const std::vector< std::size_t >& inpoel,
                const tk::UnsMesh::Coords& coord,
                const tk::Fields& U,
                tk::Fields& R ) const
    {
      Assert( U.nunk() == R.nunk(), "Number of unknowns in solution "
              "vector and right-hand side at recent time step incorrect" );
      Assert( U.nprop() == m_ncomp && R.nprop() == m_ncomp,
              "Number of components in solution and right-hand side vector " 
              "must equal "+ std::to_string(m_ncomp) );

      const auto& bface = fd.Bface();
      const auto& esuf = fd.Esuf();
      const auto& inpofa = fd.Inpofa();

      // set rhs to zero
      R.fill(0.0);

      // arrays for quadrature points
      std::array< std::vector< tk::real >, 3 > coordgp;
      std::vector< tk::real > wgp; 

      coordgp[0].resize(3,0);
      coordgp[1].resize(3,0);
      coordgp[2].resize(3,0);

      wgp.resize(3,0);

      // get quadrature point weights and coordinates for triangle
      GaussQuadrature( 2, coordgp, wgp );

      // compute internal surface flux integrals
      for (auto f=fd.Nbfac(); f<esuf.size()/2; ++f)
      {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        std::size_t er = static_cast< std::size_t >(esuf[2*f+1]);

        // nodal coordinates of the left element
        std::array< tk::real, 3 > 
          p1_l{{ coord[0][ inpoel[4*el] ],
                 coord[1][ inpoel[4*el] ],
                 coord[2][ inpoel[4*el] ] }},
          p2_l{{ coord[0][ inpoel[4*el+1] ],
                 coord[1][ inpoel[4*el+1] ],
                 coord[2][ inpoel[4*el+1] ] }},
          p3_l{{ coord[0][ inpoel[4*el+2] ],
                 coord[1][ inpoel[4*el+2] ],
                 coord[2][ inpoel[4*el+2] ] }},
          p4_l{{ coord[0][ inpoel[4*el+3] ],
                 coord[1][ inpoel[4*el+3] ],
                 coord[2][ inpoel[4*el+3] ] }};

        // nodal coordinates of the right element
        std::array< tk::real, 3 > 
          p1_r{{ coord[0][ inpoel[4*er] ],
                 coord[1][ inpoel[4*er] ],
                 coord[2][ inpoel[4*er] ] }},
          p2_r{{ coord[0][ inpoel[4*er+1] ],
                 coord[1][ inpoel[4*er+1] ],
                 coord[2][ inpoel[4*er+1] ] }},
          p3_r{{ coord[0][ inpoel[4*er+2] ],
                 coord[1][ inpoel[4*er+2] ],
                 coord[2][ inpoel[4*er+2] ] }},
          p4_r{{ coord[0][ inpoel[4*er+3] ],
                 coord[1][ inpoel[4*er+3] ],
                 coord[2][ inpoel[4*er+3] ] }};

        auto detT_l = getJacobian( p1_l, p2_l, p3_l, p4_l );
        auto detT_r = getJacobian( p1_r, p2_r, p3_r, p4_r );

        auto x1 = coord[0][ inpofa[3*f]   ];
        auto y1 = coord[1][ inpofa[3*f]   ];
        auto z1 = coord[2][ inpofa[3*f]   ];

        auto x2 = coord[0][ inpofa[3*f+1] ];
        auto y2 = coord[1][ inpofa[3*f+1] ];
        auto z2 = coord[2][ inpofa[3*f+1] ];

        auto x3 = coord[0][ inpofa[3*f+2] ];
        auto y3 = coord[1][ inpofa[3*f+2] ];
        auto z3 = coord[2][ inpofa[3*f+2] ];

        // Gaussian quadrature
        for (std::size_t igp=0; igp<3; ++igp)
        {
          // Barycentric coordinates for the triangular face
          auto shp1 = 1.0 - coordgp[0][igp] - coordgp[1][igp];
          auto shp2 = coordgp[0][igp];
          auto shp3 = coordgp[1][igp];

          // transformation of the quadrature point from the 2D reference/master
          // element to physical domain, to obtain its physical (x,y,z)
          // coordinates.
          auto xgp = x1*shp1 + x2*shp2 + x3*shp3;
          auto ygp = y1*shp1 + y2*shp2 + y3*shp3;
          auto zgp = z1*shp1 + z2*shp2 + z3*shp3;

          tk::real detT_gp(0);

          // transformation of the physical coordinates of the quadrature point
          // to reference space for the left element to be able to compute
          // basis functions on the left element.
          detT_gp = getJacobian( p1_l, {{ xgp, ygp, zgp }}, p3_l, p4_l );
          auto xi_l = detT_gp / detT_l;
          detT_gp = getJacobian( p1_l, p2_l, {{ xgp, ygp, zgp }}, p4_l );
          auto eta_l = detT_gp / detT_l;
          detT_gp = getJacobian( p1_l, p2_l, p3_l, {{ xgp, ygp, zgp }} );
          auto zeta_l = detT_gp / detT_l;

          // basis functions at igp for the left element
          auto B2l = 2.0 * xi_l + eta_l + zeta_l - 1.0;
          auto B3l = 3.0 * eta_l + zeta_l - 1.0;
          auto B4l = 4.0 * zeta_l - 1.0;

          // transformation of the physical coordinates of the quadrature point
          // to reference space for the right element
          detT_gp = getJacobian( p1_r, {{ xgp, ygp, zgp }}, p3_r, p4_r );
          auto xi_r = detT_gp / detT_r;
          detT_gp = getJacobian( p1_r, p2_r, {{ xgp, ygp, zgp }}, p4_r );
          auto eta_r = detT_gp / detT_r;
          detT_gp = getJacobian( p1_r, p2_r, p3_r, {{ xgp, ygp, zgp }} );
          auto zeta_r = detT_gp / detT_r;

          // basis functions at igp for the right element
          auto B2r = 2.0 * xi_r + eta_r + zeta_r - 1.0;
          auto B3r = 3.0 * eta_r + zeta_r - 1.0;
          auto B4r = 4.0 * zeta_r - 1.0;

          auto wt = wgp[igp] * geoFace(f,0,0);

          std::array< std::vector< tk::real >, 2 > ugp;
          
          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;
            ugp[0].push_back(  U(el, mark,   m_offset) 
                             + U(el, mark+1, m_offset) * B2l
                             + U(el, mark+2, m_offset) * B3l
                             + U(el, mark+3, m_offset) * B4l );
            ugp[1].push_back(  U(er, mark,   m_offset) 
                             + U(er, mark+1, m_offset) * B2r
                             + U(er, mark+2, m_offset) * B3r
                             + U(er, mark+3, m_offset) * B4r );
          }

          //--- upwind fluxes
          auto flux = upwindFlux( {{xgp, ygp, zgp}}, f, geoFace, ugp );

          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;

            R(el, mark,   m_offset) -= wt * flux[c];
            R(el, mark+1, m_offset) -= wt * flux[c] * B2l; 
            R(el, mark+2, m_offset) -= wt * flux[c] * B3l;
            R(el, mark+3, m_offset) -= wt * flux[c] * B4l;

            R(er, mark,   m_offset) += wt * flux[c];
            R(er, mark+1, m_offset) += wt * flux[c] * B2r;
            R(er, mark+2, m_offset) += wt * flux[c] * B3r;
            R(er, mark+3, m_offset) += wt * flux[c] * B4r;
          }
        }
      }

      // compute boundary surface flux integrals
      bndIntegralp1< Extrapolate >( m_bcextrapolate, bface, esuf, geoFace, U, R );
      bndIntegralp1< Inlet >( m_bcinlet, bface, esuf, geoFace, U, R );
      bndIntegralp1< Outlet >( m_bcoutlet, bface, esuf, geoFace, U, R );

      // resize quadrature point arrays
      coordgp[0].resize(5,0);
      coordgp[1].resize(5,0);
      coordgp[2].resize(5,0);

      wgp.resize(5,0);

      // get quadrature point weights and coordinates for tetrahedron
      GaussQuadrature( 3, coordgp, wgp );

      // compute volume integrals
      for (std::size_t e=0; e<U.nunk(); ++e)
      {
        auto x1 = coord[0][ inpoel[4*e]   ];
        auto y1 = coord[1][ inpoel[4*e]   ];
        auto z1 = coord[2][ inpoel[4*e]   ];

        auto x2 = coord[0][ inpoel[4*e+1] ];
        auto y2 = coord[1][ inpoel[4*e+1] ];
        auto z2 = coord[2][ inpoel[4*e+1] ];

        auto x3 = coord[0][ inpoel[4*e+2] ];
        auto y3 = coord[1][ inpoel[4*e+2] ];
        auto z3 = coord[2][ inpoel[4*e+2] ];

        auto x4 = coord[0][ inpoel[4*e+3] ];
        auto y4 = coord[1][ inpoel[4*e+3] ];
        auto z4 = coord[2][ inpoel[4*e+3] ];

        // Gaussian quadrature
        for (std::size_t igp=0; igp<5; ++igp)
        {
          auto B2 = 2.0 * coordgp[0][igp] + coordgp[1][igp] + coordgp[2][igp] - 1.0;
          auto B3 = 3.0 * coordgp[1][igp] + coordgp[2][igp] - 1.0;
          auto B4 = 4.0 * coordgp[2][igp] - 1.0;

          auto wt = wgp[igp] * geoElem(e, 0, 0);

          auto shp1 = 1.0 - coordgp[0][igp] - coordgp[1][igp] - coordgp[2][igp];
          auto shp2 = coordgp[0][igp];
          auto shp3 = coordgp[1][igp];
          auto shp4 = coordgp[2][igp];

          auto xgp = x1*shp1 + x2*shp2 + x3*shp3 + x4*shp4;
          auto ygp = y1*shp1 + y2*shp2 + y3*shp3 + y4*shp4;
          auto zgp = z1*shp1 + z2*shp2 + z3*shp3 + z4*shp4;

          auto db2dxi1 = 2.0;
          auto db2dxi2 = 1.0;
          auto db2dxi3 = 1.0;

          auto db3dxi1 = 0.0;
          auto db3dxi2 = 3.0;
          auto db3dxi3 = 1.0;

          auto db4dxi1 = 0.0;
          auto db4dxi2 = 0.0;
          auto db4dxi3 = 4.0;

          const auto vel = Problem::prescribedVelocity( xgp, ygp, zgp, m_c, m_ncomp );

          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;
            auto ugp =   U(e, mark,   m_offset) 
                       + U(e, mark+1, m_offset) * B2
                       + U(e, mark+2, m_offset) * B3
                       + U(e, mark+3, m_offset) * B4;

            auto fluxx = vel[c][0] * ugp;
            auto fluxy = vel[c][1] * ugp;
            auto fluxz = vel[c][2] * ugp;

            R(e, mark+1, m_offset) += wt * (fluxx * db2dxi1 + fluxy * db2dxi2 + fluxz * db2dxi3);
            R(e, mark+2, m_offset) += wt * (fluxx * db3dxi1 + fluxy * db3dxi2 + fluxz * db3dxi3);
            R(e, mark+3, m_offset) += wt * (fluxx * db4dxi1 + fluxy * db4dxi2 + fluxz * db4dxi3);
          }
        }
      }
    }

    //! Compute the minimum time step size
//     //! \param[in] U Solution vector at recent time step
//     //! \param[in] coord Mesh node coordinates
//     //! \param[in] inpoel Mesh element connectivity
    //! \return Minimum time step size
    tk::real dt( const std::array< std::vector< tk::real >, 3 >& /*coord*/,
                 const std::vector< std::size_t >& /*inpoel*/,
                 const tk::Fields& /*U*/ ) const
    {
      tk::real mindt = std::numeric_limits< tk::real >::max();
      return mindt;
    }

    //! \brief Query all side set IDs the user has configured for all components
    //!   in this PDE system
    //! \param[in,out] conf Set of unique side set IDs to add to
    void side( std::unordered_set< int >& conf ) const
    { Problem::side( conf ); }

    //! Return field names to be output to file
    //! \return Vector of strings labelling fields output in file
    //! \details This functions should be written in conjunction with
    //!   fieldOutput(), which provides the vector of fields to be output
    std::vector< std::string > fieldNames() const {
      std::vector< std::string > n;
      const auto& depvar =
        g_inputdeck.get< tag::param, tag::transport, tag::depvar >().at(m_c);
      // will output numerical solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        n.push_back( depvar + std::to_string(c) + "_numerical" );
      // will output analytic solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        n.push_back( depvar + std::to_string(c) + "_analytic" );
      // will output error for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        n.push_back( depvar + std::to_string(c) + "_error" );
      return n;
    }

    //! Return field output going to file
    //! \param[in] t Physical time
    //! \param[in] geoElem Element geometry array
    //! \param[in,out] U Solution vector at recent time step
    //! \return Vector of vectors to be output to file
    //! \details This functions should be written in conjunction with names(),
    //!   which provides the vector of field names
    //! \note U is overwritten
    std::vector< std::vector< tk::real > >
    fieldOutput( tk::real t,
                 tk::real /*V*/,
                 const tk::Fields& geoElem,
                 tk::Fields& U ) const
    {
      Assert( geoElem.nunk() == U.nunk(), "Size mismatch" );
      std::vector< std::vector< tk::real > > out;
      // will output numerical solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        out.push_back( U.extract( c, m_offset ) );
      // evaluate analytic solution at time t
      auto E = U;
      initialize( geoElem, E, t );
      // will output analytic solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        out.push_back( E.extract( c, m_offset ) );
      // will output error for all components
      for (ncomp_t c=0; c<m_ncomp; ++c) {
        auto u = U.extract( c, m_offset );
        auto e = E.extract( c, m_offset );
        for (std::size_t i=0; i<u.size(); ++i)
          e[i] = std::pow( e[i] - u[i], 2.0 ) * geoElem(i,0,0);
        out.push_back( e );
      }
      return out;
    }

    //! Return names of integral variables to be output to diagnostics file
    //! \return Vector of strings labelling integral variables output
    std::vector< std::string > names() const {
      std::vector< std::string > n;
      const auto& depvar =
        g_inputdeck.get< tag::param, tag::transport, tag::depvar >().at(m_c);
      // construct the name of the numerical solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
        n.push_back( depvar + std::to_string(c) );
      return n;
    }

  private:
    const ncomp_t m_c;                  //!< Equation system index
    const ncomp_t m_ncomp;              //!< Number of components in this PDE
    const ncomp_t m_offset;             //!< Offset this PDE operates from
    //! Extrapolation BC configuration
    const std::vector< bcconf_t > m_bcextrapolate;
    //! Inlet BC configuration
    const std::vector< bcconf_t > m_bcinlet;
    //! Outlet BC configuration
    const std::vector< bcconf_t > m_bcoutlet;
    const uint8_t m_ndof;

    //! \brief State policy class providing the left and right state of a face
    //!   at extrapolation boundaries
    struct Extrapolate {
      static std::array< std::vector< tk::real >, 2 >
      LR( const tk::Fields& U, std::size_t e ) {
        return {{ U.extract( e ), U.extract( e ) }};
      }
    };

    //! \brief State policy class providing the left and right state of a face
    //!   at inlet boundaries
    struct Inlet {
      static std::array< std::vector< tk::real >, 2 >
      LR( const tk::Fields& U, std::size_t e ) {
        auto ul = U.extract( e );
        auto ur = ul;
        std::fill( begin(ur), end(ur), 0.0 );
        return {{ std::move(ul), std::move(ur) }};
      }
    };

    //! \brief State policy class providing the left and right state of a face
    //!   at outlet boundaries
    struct Outlet {
      static std::array< std::vector< tk::real >, 2 >
      LR( const tk::Fields& U, std::size_t e ) {
        return {{ U.extract( e ), U.extract( e ) }};
      }
    };

    //! Compute boundary surface integral for a number of faces
    //! \param[in] faces Face IDs at which to compute surface integral
    //! \param[in] esuf Elements surrounding face, see tk::genEsuf()
    //! \param[in] geoFace Face geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    //! \tparam State Policy class providing the left and right state at
    //!   boundaries by its member function State::LR()
    template< class State >
    void surfInt( const std::vector< std::size_t >& faces,
                  const std::vector< int >& esuf,
                  const tk::Fields& geoFace,
                  const tk::Fields& U,
                  tk::Fields& R ) const
    {
      for (const auto& f : faces) {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        Assert( esuf[2*f+1] == -1, "outside boundary element not -1" );
        auto farea = geoFace(f,0,0);

        //--- upwind fluxes
        auto flux = upwindFlux( {{geoFace(f,4,0), geoFace(f,5,0), geoFace(f,6,0)}},
                                f, geoFace, State::LR(U,el) );

        for (ncomp_t c=0; c<m_ncomp; ++c)
          R(el, c, m_offset) -= farea * flux[c];
      }
    }

    //! Compute boundary surface flux integrals for a given boundary type
    //! \tparam BCType Specifies the type of boundary condition to apply
    //! \param bcconfig BC configuration vector for multiple side sets
    //! \param[in] bface Boundary faces side-set information
    //! \param[in] esuf Elements surrounding faces
    //! \param[in] geoFace Face geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    template< class BCType >
    void
    bndIntegral( const std::vector< bcconf_t >& bcconfig,
                 const std::map< int, std::vector< std::size_t > >& bface,
                 const std::vector< int >& esuf,
                 const tk::Fields& geoFace,
                 const tk::Fields& U,
                 tk::Fields& R ) const
    {
      for (const auto& s : bcconfig) {       // for all bc sidesets
        auto bc = bface.find( std::stoi(s) );// faces for side set
        if (bc != end(bface))
          surfInt< BCType >( bc->second, esuf, geoFace, U, R );
      }
    }

    //! Compute boundary surface integral for a number of faces
    //! \param[in] faces Face IDs at which to compute surface integral
    //! \param[in] esuf Elements surrounding face, see tk::genEsuf()
    //! \param[in] geoFace Face geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    //! \tparam State Policy class providing the left and right state at
    //!   boundaries by its member function State::LR()
    template< class State >
    void surfIntp1( const std::vector< std::size_t >& faces,
                    const std::vector< int >& esuf,
                    const tk::Fields& geoFace,
                    const tk::Fields& U,
                    tk::Fields& R ) const
    {
      // arrays for quadrature points
      std::array< std::vector< tk::real >, 3 > coordgp;
      std::vector< tk::real > wgp; 

      coordgp[0].resize(3,0);
      coordgp[1].resize(3,0);
      coordgp[2].resize(3,0);

      wgp.resize(3,0);

      // get quadrature point weights and coordinates for triangle
      GaussQuadrature( 2, coordgp, wgp );

      for (const auto& f : faces) {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        Assert( esuf[2*f+1] == -1, "outside boundary element not -1" );
        auto farea = geoFace(f,0,0);

        // nodal coordinates of the left element
        std::array< tk::real, 3 > 
          p1_l{{ coord[0][ inpoel[4*el] ],
                 coord[1][ inpoel[4*el] ],
                 coord[2][ inpoel[4*el] ] }},
          p2_l{{ coord[0][ inpoel[4*el+1] ],
                 coord[1][ inpoel[4*el+1] ],
                 coord[2][ inpoel[4*el+1] ] }},
          p3_l{{ coord[0][ inpoel[4*el+2] ],
                 coord[1][ inpoel[4*el+2] ],
                 coord[2][ inpoel[4*el+2] ] }},
          p4_l{{ coord[0][ inpoel[4*el+3] ],
                 coord[1][ inpoel[4*el+3] ],
                 coord[2][ inpoel[4*el+3] ] }};

        auto detT_l = getJacobian( p1_l, p2_l, p3_l, p4_l );

        auto x1 = coord[0][ inpofa[3*f]   ];
        auto y1 = coord[1][ inpofa[3*f]   ];
        auto z1 = coord[2][ inpofa[3*f]   ];

        auto x2 = coord[0][ inpofa[3*f+1] ];
        auto y2 = coord[1][ inpofa[3*f+1] ];
        auto z2 = coord[2][ inpofa[3*f+1] ];

        auto x3 = coord[0][ inpofa[3*f+2] ];
        auto y3 = coord[1][ inpofa[3*f+2] ];
        auto z3 = coord[2][ inpofa[3*f+2] ];

        // Gaussian quadrature
        for (std::size_t igp=0; igp<3; ++igp)
        {
          // Barycentric coordinates for the triangular face
          auto shp1 = 1.0 - coordgp[0][igp] - coordgp[1][igp];
          auto shp2 = coordgp[0][igp];
          auto shp3 = coordgp[1][igp];

          // transformation of the quadrature point from the 2D reference/master
          // element to physical domain, to obtain its physical (x,y,z)
          // coordinates.
          auto xgp = x1*shp1 + x2*shp2 + x3*shp3;
          auto ygp = y1*shp1 + y2*shp2 + y3*shp3;
          auto zgp = z1*shp1 + z2*shp2 + z3*shp3;

          tk::real detT_gp(0);

          // transformation of the physical coordinates of the quadrature point
          // to reference space for the left element to be able to compute
          // basis functions on the left element.
          detT_gp = getJacobian( p1_l, {{ xgp, ygp, zgp }}, p3_l, p4_l );
          auto xi_l = detT_gp / detT_l;
          detT_gp = getJacobian( p1_l, p2_l, {{ xgp, ygp, zgp }}, p4_l );
          auto eta_l = detT_gp / detT_l;
          detT_gp = getJacobian( p1_l, p2_l, p3_l, {{ xgp, ygp, zgp }} );
          auto zeta_l = detT_gp / detT_l;

          // basis functions at igp for the left element
          auto B2l = 2.0 * xi_l + eta_l + zeta_l - 1.0;
          auto B3l = 3.0 * eta_l + zeta_l - 1.0;
          auto B4l = 4.0 * zeta_l - 1.0;

          auto wt = wgp[igp] * geoFace(f,0,0);

          std::array< std::vector< tk::real >, 2 > ugp;
          
          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;
            ugp[0].push_back(  U(el, mark,   m_offset) 
                             + U(el, mark+1, m_offset) * B2l
                             + U(el, mark+2, m_offset) * B3l
                             + U(el, mark+3, m_offset) * B4l );
            ugp[1].push_back(  U(er, mark,   m_offset) 
                             + U(er, mark+1, m_offset) * B2r
                             + U(er, mark+2, m_offset) * B3r
                             + U(er, mark+3, m_offset) * B4r );
          }

          //--- upwind fluxes
          auto flux = upwindFlux( {{xgp, ygp, zgp}}, f, geoFace, ugp );

          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;

            R(el, mark,   m_offset) -= wt * flux[c];
            R(el, mark+1, m_offset) -= wt * flux[c] * B2l; 
            R(el, mark+2, m_offset) -= wt * flux[c] * B3l;
            R(el, mark+3, m_offset) -= wt * flux[c] * B4l;
          }
        }

        //--- upwind fluxes
        auto flux = upwindFlux( {{geoFace(f,4,0), geoFace(f,5,0), geoFace(f,6,0)}},
                                f, geoFace, State::LR(U,el) );
      }
    }

    //! Compute boundary surface flux integrals for a given boundary type
    //! \tparam BCType Specifies the type of boundary condition to apply
    //! \param bcconfig BC configuration vector for multiple side sets
    //! \param[in] bface Boundary faces side-set information
    //! \param[in] esuf Elements surrounding faces
    //! \param[in] geoFace Face geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    template< class BCType >
    void
    bndIntegralp1( const std::vector< bcconf_t >& bcconfig,
                   const std::map< int, std::vector< std::size_t > >& bface,
                   const std::vector< int >& esuf,
                   const tk::Fields& geoFace,
                   const tk::Fields& U,
                   tk::Fields& R ) const
    {
      for (const auto& s : bcconfig) {       // for all bc sidesets
        auto bc = bface.find( std::stoi(s) );// faces for side set
        if (bc != end(bface))
          surfIntp1< BCType >( bc->second, esuf, geoFace, U, R );
      }
    }

    //! Riemann solver using upwind method
    //! \param[in] f Face ID
    //! \param[in] geoFace Face geometry array
    //! \param[in] u Left and right unknown/state vector
    //! \return Riemann solution using upwind method
    std::vector< tk::real >
    upwindFlux( std::array< tk::real, 3> centcoord,
                std::size_t f,
                const tk::Fields& geoFace,
                const std::array< std::vector< tk::real >, 2 >& u ) const
    {
      std::vector< tk::real > flux( u[0].size(), 0 );

      auto xc = centcoord[0];
      auto yc = centcoord[1];
      auto zc = centcoord[2];

      std::array< tk::real, 3 > fn {{ geoFace(f,1,0),
                                      geoFace(f,2,0),
                                      geoFace(f,3,0) }};

      const auto vel = Problem::prescribedVelocity( xc, yc, zc, m_c, m_ncomp );
    
      for(ncomp_t c=0; c<m_ncomp; ++c)
      {
        auto ax = vel[c][0];
        auto ay = vel[c][1];
        auto az = vel[c][2];

        // wave speed
        tk::real swave = ax*fn[0] + ay*fn[1] + az*fn[2];
    
        // upwinding
        tk::real splus  = 0.5 * (swave + fabs(swave));
        tk::real sminus = 0.5 * (swave - fabs(swave));
    
        flux[c] = splus * u[0][c] + sminus * u[1][c];
      }
    
      return flux;
    }

    //! Gaussian quadrature points locations and weights
    //! \param[in] ndimn Dimension of integration domain
    //! \param[in,out] coordgp Coordinates of quadrature points
    //! \param[in,out] wgp Weights of quadrature points
    void
    GaussQuadrature( uint8_t ndimn,
                     std::array< std::vector< tk::real >, 3 >& coordgp,
                     std::vector< tk::real >& wgp ) const
    {
      if (ndimn == 3)
        {
          coordgp[0][0] = 0.25;
          coordgp[1][0] = 0.25;
          coordgp[2][0] = 0.25;
          wgp[0]        = -12.0/15.0;

          coordgp[0][1] = 1.0/6.0;
          coordgp[1][1] = 1.0/6.0;
          coordgp[2][1] = 1.0/6.0;
          wgp[1]        = 9.0/20.0;

          coordgp[0][2] = 0.5;
          coordgp[1][2] = 1.0/6.0;
          coordgp[2][2] = 1.0/6.0;
          wgp[2]        = 9.0/20.0;

          coordgp[0][3] = 1.0/6.0;
          coordgp[1][3] = 0.5;
          coordgp[2][3] = 1.0/6.0;
          wgp[3]        = 9.0/20.0;

          coordgp[0][4] = 1.0/6.0;
          coordgp[1][4] = 1.0/6.0;
          coordgp[2][4] = 0.5;
          wgp[4]        = 9.0/20.0;
        }
      else if (ndimn == 2)
        {
          coordgp[0][0] = 2.0/3.0;
          coordgp[1][0] = 1.0/6.0;
          wgp[0]        = 1.0/3.0;

          coordgp[0][1] = 1.0/6.0;
          coordgp[1][1] = 2.0/3.0;
          wgp[1]        = 1.0/3.0;

          coordgp[0][2] = 1.0/6.0;
          coordgp[1][2] = 1.0/6.0;
          wgp[2]        = 1.0/3.0;
        }
      else
        {
          std::cout << "Incorrect dimensionality input to GaussQuadrature\n";
        }
    }

    //! Determinant of Jacobian of transformation
    //! \param[in] p1 (x,y,z) coordinates of 1st local node in the tetrahedron
    //! \param[in] p2 (x,y,z) coordinates of 2nd local node in the tetrahedron
    //! \param[in] p3 (x,y,z) coordinates of 3rd local node in the tetrahedron
    //! \param[in] p4 (x,y,z) coordinates of 4th local node in the tetrahedron
    //! \return Determinant of the Jacobian of transformation of physical
    //!   tetrahedron to reference (xi, eta, zeta) space
    tk::real
    getJacobian( const std::array< tk::real, 3 >& p1,
                 const std::array< tk::real, 3 >& p2,
                 const std::array< tk::real, 3 >& p3,
                 const std::array< tk::real, 3 >& p4 ) const
    {
      tk::real detT;

      detT = (p2[0]-p1[0])
              * ((p3[1]-p1[1])*(p4[2]-p1[2]) - (p4[1]-p1[1])*(p3[2]-p1[2]))
            -(p3[0]-p1[0])
              * ((p2[1]-p1[1])*(p4[2]-p1[2]) - (p4[1]-p1[1])*(p2[2]-p1[2]))
            +(p4[0]-p1[0])
              * ((p2[1]-p1[1])*(p3[2]-p1[2]) - (p3[1]-p1[1])*(p2[2]-p1[2]));

      return detT;
    }

};

} // dg::
} // inciter::

#endif // DGTransport_h
