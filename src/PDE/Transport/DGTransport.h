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
#include "Quadrature.h"
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
      m_bcdir( config< tag::bcdir >( c ) ),
      m_ndof( g_inputdeck.get< tag::discr, tag::ndof >() )
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
        {
          auto mark = c*m_ndof;
          unk(e, mark, m_offset) = s[c];
        }
      }
    }

    //! Initalize the transport equations for DGP1 using problem policy
    //! \param[in] lhs Element mass matrix
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in,out] unk Array of unknowns
    //! \param[in] t Physical time
    void initializep1( const tk::Fields& lhs,
                       const std::vector< std::size_t >& inpoel,
                       const tk::UnsMesh::Coords& coord,
                       tk::Fields& unk,
                       tk::real t ) const
    {
      Assert( lhs.nunk() == unk.nunk(), "Size mismatch" );
      std::size_t nelem = unk.nunk();

      // right hand side vector
      std::vector< tk::real > R;
      R.resize(unk.nprop(),0);

      // arrays for quadrature points
      std::array< std::array< tk::real, 5 >, 3 > coordgp;
      std::array< tk::real, 5 > wgp;

      const auto& cx = coord[0];
      const auto& cy = coord[1];
      const auto& cz = coord[2];

      // get quadrature point weights and coordinates for tetrahedron
      GaussQuadratureTet( coordgp, wgp );

      for (std::size_t e=0; e<nelem; ++e)
      {
        auto vole = lhs(e, 0, m_offset);

        auto x1 = cx[ inpoel[4*e]   ];
        auto y1 = cy[ inpoel[4*e]   ];
        auto z1 = cz[ inpoel[4*e]   ];

        auto x2 = cx[ inpoel[4*e+1] ];
        auto y2 = cy[ inpoel[4*e+1] ];
        auto z2 = cz[ inpoel[4*e+1] ];

        auto x3 = cx[ inpoel[4*e+2] ];
        auto y3 = cy[ inpoel[4*e+2] ];
        auto z3 = cz[ inpoel[4*e+2] ];

        auto x4 = cx[ inpoel[4*e+3] ];
        auto y4 = cy[ inpoel[4*e+3] ];
        auto z4 = cz[ inpoel[4*e+3] ];

        std::fill( R.begin(), R.end(), 0.0);

        // Gaussian quadrature
        for (std::size_t igp=0; igp<5; ++igp)
        {
          auto B2 = 2.0 * coordgp[0][igp] + coordgp[1][igp] + coordgp[2][igp]
                    - 1.0;
          auto B3 = 3.0 * coordgp[1][igp] + coordgp[2][igp] - 1.0;
          auto B4 = 4.0 * coordgp[2][igp] - 1.0;

          auto shp1 = 1.0 - coordgp[0][igp] - coordgp[1][igp] - coordgp[2][igp];
          auto shp2 = coordgp[0][igp];
          auto shp3 = coordgp[1][igp];
          auto shp4 = coordgp[2][igp];

          auto xgp = x1*shp1 + x2*shp2 + x3*shp3 + x4*shp4;
          auto ygp = y1*shp1 + y2*shp2 + y3*shp3 + y4*shp4;
          auto zgp = z1*shp1 + z2*shp2 + z3*shp3 + z4*shp4;

          auto wt = vole * wgp[igp];

          const auto s = Problem::solution( m_c, m_ncomp, xgp, ygp, zgp, t );
          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;

            R[mark  ] += wt * s[c];
            R[mark+1] += wt * s[c]*B2;
            R[mark+2] += wt * s[c]*B3;
            R[mark+3] += wt * s[c]*B4;
          }
        }

        for (ncomp_t c=0; c<m_ncomp; ++c)
        {
          auto mark = c*m_ndof;
          unk(e, mark,   m_offset) = R[mark]   / lhs(e, mark,   m_offset);
          unk(e, mark+1, m_offset) = R[mark+1] / lhs(e, mark+1, m_offset);
          unk(e, mark+2, m_offset) = R[mark+2] / lhs(e, mark+2, m_offset);
          unk(e, mark+3, m_offset) = R[mark+3] / lhs(e, mark+3, m_offset);
        }
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
        {
          auto mark = c*m_ndof;
          l(e, mark, m_offset) = geoElem(e,0,0);
        }
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
    //! \param[in] t Physical time
    //! \param[in] geoFace Face geometry array
    //! \param[in] fd Face connectivity and boundary conditions object
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void rhs( tk::real t,
              const tk::Fields& geoFace,
              const tk::Fields&,
              const inciter::FaceData& fd,
              const tk::Fields& U,
              tk::Fields& R ) const
    {
      Assert( U.nunk() == R.nunk(), "Number of unknowns in solution "
              "vector and right-hand side at recent time step incorrect" );
      Assert( U.nprop() == m_ndof*m_ncomp && R.nprop() == m_ndof*m_ncomp,
              "Number of components in solution and right-hand side vector " 
              "must equal "+ std::to_string(m_ndof*m_ncomp) );

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

        std::array< std::vector< tk::real >, 2 > ugp;
        for (ncomp_t c=0; c<m_ncomp; ++c)
        {
          auto mark = c*m_ndof;
          ugp[0].push_back( U(el, mark, m_offset) );
          ugp[1].push_back( U(er, mark, m_offset) );
        }

        //--- upwind fluxes
        auto flux =
          upwindFlux( {{geoFace(f,4,0), geoFace(f,5,0), geoFace(f,6,0)}}, f,
                      geoFace, ugp );

        for (ncomp_t c=0; c<m_ncomp; ++c) {
          auto mark = c*m_ndof;
          R(el, mark, m_offset) -= farea * flux[c];
          R(er, mark, m_offset) += farea * flux[c];
        }
      }

      // compute boundary surface flux integrals
      bndIntegral< Extrapolate >( m_bcextrapolate, bface, esuf, geoFace, t, U, R );
      bndIntegral< Inlet >( m_bcinlet, bface, esuf, geoFace, t, U, R );
      bndIntegral< Outlet >( m_bcoutlet, bface, esuf, geoFace, t, U, R );
      bndIntegral< Dir >( m_bcdir, bface, esuf, geoFace, t, U, R );
    }

    //! Compute P1 right hand side
    //! \param[in] t Physical time
    //! \param[in] geoFace Face geometry array
    //! \param[in] geoElem Element geometry array
    //! \param[in] fd Face connectivity and boundary conditions object
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void rhsp1( tk::real t,
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
      Assert( U.nprop() == m_ndof*m_ncomp && R.nprop() == m_ndof*m_ncomp,
              "Number of components in solution and right-hand side vector " 
              "must equal "+ std::to_string(m_ncomp) );
      Assert( inpoel.size()/4 == U.nunk(), "Connectivity inpoel has incorrect "
              "size" );

      const auto& bface = fd.Bface();
      const auto& esuf = fd.Esuf();
      const auto& inpofa = fd.Inpofa();

      Assert( inpofa.size()/3 == esuf.size()/2, "Mismatch in inpofa size" );

      // set rhs to zero
      R.fill(0.0);

      // compute surface flux integrals
      surfInt( inpoel, coord, fd, geoFace, U, R );

      // compute boundary surface flux integrals
      bndIntegralp1< Extrapolate >( m_bcextrapolate, bface, esuf, geoFace,
                                    inpoel, inpofa, coord, t, U, R );
      bndIntegralp1< Inlet >( m_bcinlet, bface, esuf, geoFace, inpoel, inpofa,
                              coord, t, U, R );
      bndIntegralp1< Outlet >( m_bcoutlet, bface, esuf, geoFace, inpoel,
                               inpofa, coord, t, U, R );
      bndIntegralp1< Dir >( m_bcdir, bface, esuf, geoFace, inpoel,
                            inpofa, coord, t, U, R );

      // compute volume integrals
      volInt( inpoel, coord, geoElem, U, R );
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
      {
        auto mark = c*m_ndof;
        out.push_back( U.extract( mark, m_offset ) );
      }
      // evaluate analytic solution at time t
      auto E = U;
      initialize( geoElem, E, t );
      // will output analytic solution for all components
      for (ncomp_t c=0; c<m_ncomp; ++c)
      {
        auto mark = c*m_ndof;
        out.push_back( E.extract( mark, m_offset ) );
      }
      // will output error for all components
      for (ncomp_t c=0; c<m_ncomp; ++c) {
        auto mark = c*m_ndof;
        auto u = U.extract( mark, m_offset );
        auto e = E.extract( mark, m_offset );
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

    //! Return analytical solution at the given (xi,yi,zi) location at time t
    //! \param[in] xi X-coordinate
    //! \param[in] yi Y-coordinate
    //! \param[in] zi Z-coordinate
    //! \param[in] t Physical time
    //! \return Vector of analytical solution at given location and time
    std::vector< tk::real >
    analyticalSol( tk::real xi,
                   tk::real yi,
                   tk::real zi,
                   tk::real t ) const
    {
      const auto s = Problem::solution( m_c, m_ncomp, xi, yi, zi, t );
      return s;
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
    //! Dirichlet BC configuration
    const std::vector< bcconf_t > m_bcdir;
    const std::size_t m_ndof;


    //! Compute internal surface flux integrals
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] fd Face connectivity and boundary conditions object
    //! \param[in] geoFace Face geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void surfInt( const std::vector< std::size_t >& inpoel,
                  const tk::UnsMesh::Coords& coord,
                  const inciter::FaceData& fd,
                  const tk::Fields& geoFace,
                  const tk::Fields& U,
                  tk::Fields& R ) const
    {
      const auto& esuf = fd.Esuf();
      const auto& inpofa = fd.Inpofa();

      // arrays for quadrature points
      std::array< std::array< tk::real, 3 >, 2 > coordgp;
      std::array< tk::real, 3 > wgp; 

      const auto& cx = coord[0];
      const auto& cy = coord[1];
      const auto& cz = coord[2];

      // get quadrature point weights and coordinates for triangle
      GaussQuadratureTri( coordgp, wgp );

      // compute internal surface flux integrals
      for (auto f=fd.Nbfac(); f<esuf.size()/2; ++f)
      {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        std::size_t er = static_cast< std::size_t >(esuf[2*f+1]);

        // nodal coordinates of the left element
        std::array< tk::real, 3 > 
          p1_l{{ cx[ inpoel[4*el] ],
                 cy[ inpoel[4*el] ],
                 cz[ inpoel[4*el] ] }},
          p2_l{{ cx[ inpoel[4*el+1] ],
                 cy[ inpoel[4*el+1] ],
                 cz[ inpoel[4*el+1] ] }},
          p3_l{{ cx[ inpoel[4*el+2] ],
                 cy[ inpoel[4*el+2] ],
                 cz[ inpoel[4*el+2] ] }},
          p4_l{{ cx[ inpoel[4*el+3] ],
                 cy[ inpoel[4*el+3] ],
                 cz[ inpoel[4*el+3] ] }};

        // nodal coordinates of the right element
        std::array< tk::real, 3 > 
          p1_r{{ cx[ inpoel[4*er] ],
                 cy[ inpoel[4*er] ],
                 cz[ inpoel[4*er] ] }},
          p2_r{{ cx[ inpoel[4*er+1] ],
                 cy[ inpoel[4*er+1] ],
                 cz[ inpoel[4*er+1] ] }},
          p3_r{{ cx[ inpoel[4*er+2] ],
                 cy[ inpoel[4*er+2] ],
                 cz[ inpoel[4*er+2] ] }},
          p4_r{{ cx[ inpoel[4*er+3] ],
                 cy[ inpoel[4*er+3] ],
                 cz[ inpoel[4*er+3] ] }};

        auto detT_l = getJacobian( p1_l, p2_l, p3_l, p4_l );
        auto detT_r = getJacobian( p1_r, p2_r, p3_r, p4_r );

        auto x1 = cx[ inpofa[3*f]   ];
        auto y1 = cy[ inpofa[3*f]   ];
        auto z1 = cz[ inpofa[3*f]   ];

        auto x2 = cx[ inpofa[3*f+1] ];
        auto y2 = cy[ inpofa[3*f+1] ];
        auto z2 = cz[ inpofa[3*f+1] ];

        auto x3 = cx[ inpofa[3*f+2] ];
        auto y3 = cy[ inpofa[3*f+2] ];
        auto z3 = cz[ inpofa[3*f+2] ];

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

          // The basis functions chosen for the DG method are the Dubiner
          // basis, which are Legendre polynomials modified for tetrahedra,
          // which are defined only on the reference/master tetrahedron.
          // Thus, to determine the high-order solution from the left and right
          // elements at the surface quadrature points, the basis functions
          // from the left and right elements are needed. For this, a
          // transformation to the reference coordinates is necessary, since
          // the basis functions are defined on the reference tetrahedron only.
          // Ref: [1] https://doi.org/10.1007/BF01060030
          //      [2] https://doi.org/10.1093/imamat/hxh111

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
    }

    //! Compute volume integrals
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] geoElem Element geometry array
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    void volInt( const std::vector< std::size_t >& inpoel,
                 const tk::UnsMesh::Coords& coord,
                 const tk::Fields& geoElem,
                 const tk::Fields& U,
                 tk::Fields& R ) const
    {
      // arrays for quadrature points
      std::array< std::array< tk::real, 5 >, 3 > coordgp;
      std::array< tk::real, 5 > wgp;

      // get quadrature point weights and coordinates for tetrahedron
      GaussQuadratureTet( coordgp, wgp );

      std::array< std::array< tk::real, 3 >, 3 > jacInv;

      const auto& cx = coord[0];
      const auto& cy = coord[1];
      const auto& cz = coord[2];

      // compute volume integrals
      for (std::size_t e=0; e<U.nunk(); ++e)
      {
        auto x1 = cx[ inpoel[4*e]   ];
        auto y1 = cy[ inpoel[4*e]   ];
        auto z1 = cz[ inpoel[4*e]   ];

        auto x2 = cx[ inpoel[4*e+1] ];
        auto y2 = cy[ inpoel[4*e+1] ];
        auto z2 = cz[ inpoel[4*e+1] ];

        auto x3 = cx[ inpoel[4*e+2] ];
        auto y3 = cy[ inpoel[4*e+2] ];
        auto z3 = cz[ inpoel[4*e+2] ];

        auto x4 = cx[ inpoel[4*e+3] ];
        auto y4 = cy[ inpoel[4*e+3] ];
        auto z4 = cz[ inpoel[4*e+3] ];

        jacInv = getJacInverse( {{x1, y1, z1}},
                                {{x2, y2, z2}},
                                {{x3, y3, z3}},
                                {{x4, y4, z4}} );

        // The derivatives of the basis functions dB/dx are easily calculated
        // via a transformation to the reference space as,
        // dB/dx = dB/dX . dx/dxi,
        // where, x = (x,y,z) are the physical coordinates, and
        //        xi = (xi, eta, zeta) are the reference coordinates.
        // The matrix dx/dxi is the inverse of the Jacobian of transformation
        // and the matrix vector product has to be calculated. This follows.

        auto db2dxi1 = 2.0;
        auto db2dxi2 = 1.0;
        auto db2dxi3 = 1.0;

        auto db3dxi1 = 0.0;
        auto db3dxi2 = 3.0;
        auto db3dxi3 = 1.0;

        auto db4dxi1 = 0.0;
        auto db4dxi2 = 0.0;
        auto db4dxi3 = 4.0;

        auto db2dx =  db2dxi1 * jacInv[0][0]
                    + db2dxi2 * jacInv[1][0]
                    + db2dxi3 * jacInv[2][0];

        auto db2dy =  db2dxi1 * jacInv[0][1]
                    + db2dxi2 * jacInv[1][1]
                    + db2dxi3 * jacInv[2][1];

        auto db2dz =  db2dxi1 * jacInv[0][2]
                    + db2dxi2 * jacInv[1][2]
                    + db2dxi3 * jacInv[2][2];

        auto db3dx =  db3dxi1 * jacInv[0][0]
                    + db3dxi2 * jacInv[1][0]
                    + db3dxi3 * jacInv[2][0];

        auto db3dy =  db3dxi1 * jacInv[0][1]
                    + db3dxi2 * jacInv[1][1]
                    + db3dxi3 * jacInv[2][1];

        auto db3dz =  db3dxi1 * jacInv[0][2]
                    + db3dxi2 * jacInv[1][2]
                    + db3dxi3 * jacInv[2][2];

        auto db4dx =  db4dxi1 * jacInv[0][0]
                    + db4dxi2 * jacInv[1][0]
                    + db4dxi3 * jacInv[2][0];

        auto db4dy =  db4dxi1 * jacInv[0][1]
                    + db4dxi2 * jacInv[1][1]
                    + db4dxi3 * jacInv[2][1];

        auto db4dz =  db4dxi1 * jacInv[0][2]
                    + db4dxi2 * jacInv[1][2]
                    + db4dxi3 * jacInv[2][2];

        // Gaussian quadrature
        for (std::size_t igp=0; igp<5; ++igp)
        {
          auto B2 = 2.0 * coordgp[0][igp] + coordgp[1][igp]
                    + coordgp[2][igp] - 1.0;
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

          const auto vel =
            Problem::prescribedVelocity( xgp, ygp, zgp, m_c, m_ncomp );

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

            R(e, mark+1, m_offset) +=
              wt * (fluxx * db2dx + fluxy * db2dy + fluxz * db2dz);
            R(e, mark+2, m_offset) +=
              wt * (fluxx * db3dx + fluxy * db3dy + fluxz * db3dz);
            R(e, mark+3, m_offset) +=
              wt * (fluxx * db4dx + fluxy * db4dy + fluxz * db4dz);
          }
        }
      }
    }

    //! \brief State policy class providing the left and right state of a face
    //!   at extrapolation boundaries
    struct Extrapolate {
      static std::array< std::vector< tk::real >, 2 >
      LR( ncomp_t /*ncomp*/,
          const std::vector< tk::real >& ul,
          tk::real /*xc*/, tk::real /*yc*/, tk::real /*zc*/,
          tk::real /*t*/ ) {
        return {{ ul, ul }};
      }
    };

    //! \brief State policy class providing the left and right state of a face
    //!   at inlet boundaries
    struct Inlet {
      static std::array< std::vector< tk::real >, 2 >
      LR( ncomp_t /*ncomp*/,
          const std::vector< tk::real >& ul,
          tk::real /*xc*/, tk::real /*yc*/, tk::real /*zc*/,
          tk::real /*t*/ ) {
        auto ur = ul;
        std::fill( begin(ur), end(ur), 0.0 );
        return {{ std::move(ul), std::move(ur) }};
      }
    };

    //! \brief State policy class providing the left and right state of a face
    //!   at outlet boundaries
    struct Outlet {
      static std::array< std::vector< tk::real >, 2 >
      LR( ncomp_t /*ncomp*/,
          const std::vector< tk::real >& ul,
          tk::real /*xc*/, tk::real /*yc*/, tk::real /*zc*/,
          tk::real /*t*/ ) {
        return {{ ul, ul }};
      }
    };

    //! \brief State policy class providing the left and right state of a face
    //!   at Dirichlet boundaries
    struct Dir {
      static std::array< std::vector< tk::real >, 2 >
      LR( ncomp_t ncomp,
          const std::vector< tk::real >& ul,
          tk::real xc, tk::real yc, tk::real zc,
          tk::real t ) {
        auto ur = ul;
        const auto urbc = Problem::solution(0, ncomp, xc, yc, zc, t);
        for (ncomp_t c=0; c<ncomp; ++c)
          ur[c] = urbc[c];
        return {{ ul, ur }};
      }
    };

    //! Compute boundary surface integral for a number of faces
    //! \param[in] faces Face IDs at which to compute surface integral
    //! \param[in] esuf Elements surrounding face, see tk::genEsuf()
    //! \param[in] geoFace Face geometry array
    //! \param[in] t Physical time
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    //! \tparam State Policy class providing the left and right state at
    //!   boundaries by its member function State::LR()
    template< class State >
    void bndSurfInt( const std::vector< std::size_t >& faces,
                     const std::vector< int >& esuf,
                     const tk::Fields& geoFace,
                     tk::real t,
                     const tk::Fields& U,
                     tk::Fields& R ) const
    {
      for (const auto& f : faces) {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        Assert( esuf[2*f+1] == -1, "outside boundary element not -1" );
        auto farea = geoFace(f,0,0);

        std::vector< tk::real > ugp;
        for (ncomp_t c=0; c<m_ncomp; ++c)
        {
          auto mark = c*m_ndof;
          ugp.push_back( U(el, mark, m_offset) );
        }

        //--- upwind fluxes
        auto flux =
          upwindFlux( {{geoFace(f,4,0), geoFace(f,5,0), geoFace(f,6,0)}},
                      f,
                      geoFace,
                      State::LR( m_ncomp, ugp, geoFace(f,4,0), geoFace(f,5,0),
                                 geoFace(f,6,0), t ) );

        for (ncomp_t c=0; c<m_ncomp; ++c)
        {
          auto mark = c*m_ndof;
          R(el, mark, m_offset) -= farea * flux[c];
        }
      }
    }

    //! Compute boundary surface flux integrals for a given boundary type
    //! \tparam BCType Specifies the type of boundary condition to apply
    //! \param bcconfig BC configuration vector for multiple side sets
    //! \param[in] bface Boundary faces side-set information
    //! \param[in] esuf Elements surrounding faces
    //! \param[in] geoFace Face geometry array
    //! \param[in] t Physical time
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    template< class BCType >
    void
    bndIntegral( const std::vector< bcconf_t >& bcconfig,
                 const std::map< int, std::vector< std::size_t > >& bface,
                 const std::vector< int >& esuf,
                 const tk::Fields& geoFace,
                 tk::real t,
                 const tk::Fields& U,
                 tk::Fields& R ) const
    {
      for (const auto& s : bcconfig) {       // for all bc sidesets
        auto bc = bface.find( std::stoi(s) );// faces for side set
        if (bc != end(bface))
          bndSurfInt< BCType >( bc->second, esuf, geoFace, t, U, R );
      }
    }

    //! Compute boundary surface integral for a number of faces
    //! \param[in] faces Face IDs at which to compute surface integral
    //! \param[in] esuf Elements surrounding face, see tk::genEsuf()
    //! \param[in] geoFace Face geometry array
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] inpofa Face-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] t Physical time
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    //! \tparam State Policy class providing the left and right state at
    //!   boundaries by its member function State::LR()
    template< class State >
    void bndSurfIntp1( const std::vector< std::size_t >& faces,
                       const std::vector< int >& esuf,
                       const tk::Fields& geoFace,
                       const std::vector< std::size_t >& inpoel,
                       const std::vector< std::size_t >& inpofa,
                       const tk::UnsMesh::Coords& coord,
                       tk::real t,
                       const tk::Fields& U,
                       tk::Fields& R ) const
    {
      // arrays for quadrature points
      std::array< std::array< tk::real, 3 >, 2 > coordgp;
      std::array< tk::real, 3 > wgp;

      const auto& cx = coord[0];
      const auto& cy = coord[1];
      const auto& cz = coord[2];

      // get quadrature point weights and coordinates for triangle
      GaussQuadratureTri( coordgp, wgp );

      for (const auto& f : faces) {
        std::size_t el = static_cast< std::size_t >(esuf[2*f]);
        Assert( esuf[2*f+1] == -1, "outside boundary element not -1" );

        // nodal coordinates of the left element
        std::array< tk::real, 3 > 
          p1_l{{ cx[ inpoel[4*el] ],
                 cy[ inpoel[4*el] ],
                 cz[ inpoel[4*el] ] }},
          p2_l{{ cx[ inpoel[4*el+1] ],
                 cy[ inpoel[4*el+1] ],
                 cz[ inpoel[4*el+1] ] }},
          p3_l{{ cx[ inpoel[4*el+2] ],
                 cy[ inpoel[4*el+2] ],
                 cz[ inpoel[4*el+2] ] }},
          p4_l{{ cx[ inpoel[4*el+3] ],
                 cy[ inpoel[4*el+3] ],
                 cz[ inpoel[4*el+3] ] }};

        auto detT_l = getJacobian( p1_l, p2_l, p3_l, p4_l );

        auto x1 = cx[ inpofa[3*f]   ];
        auto y1 = cy[ inpofa[3*f]   ];
        auto z1 = cz[ inpofa[3*f]   ];

        auto x2 = cx[ inpofa[3*f+1] ];
        auto y2 = cy[ inpofa[3*f+1] ];
        auto z2 = cz[ inpofa[3*f+1] ];

        auto x3 = cx[ inpofa[3*f+2] ];
        auto y3 = cy[ inpofa[3*f+2] ];
        auto z3 = cz[ inpofa[3*f+2] ];

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

          std::vector< tk::real > ugp;
          
          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;
            ugp.push_back(  U(el, mark,   m_offset) 
                          + U(el, mark+1, m_offset) * B2l
                          + U(el, mark+2, m_offset) * B3l
                          + U(el, mark+3, m_offset) * B4l );
          }

          //--- upwind fluxes
          auto flux = upwindFlux( {{xgp, ygp, zgp}}, f, geoFace,
                                  State::LR(m_ncomp, ugp, xgp, ygp, zgp, t) );

          for (ncomp_t c=0; c<m_ncomp; ++c)
          {
            auto mark = c*m_ndof;

            R(el, mark,   m_offset) -= wt * flux[c];
            R(el, mark+1, m_offset) -= wt * flux[c] * B2l; 
            R(el, mark+2, m_offset) -= wt * flux[c] * B3l;
            R(el, mark+3, m_offset) -= wt * flux[c] * B4l;
          }
        }
      }
    }

    //! Compute boundary surface flux integrals for a given boundary type
    //! \tparam BCType Specifies the type of boundary condition to apply
    //! \param bcconfig BC configuration vector for multiple side sets
    //! \param[in] bface Boundary faces side-set information
    //! \param[in] esuf Elements surrounding faces
    //! \param[in] geoFace Face geometry array
    //! \param[in] inpoel Element-node connectivity
    //! \param[in] inpofa Face-node connectivity
    //! \param[in] coord Array of nodal coordinates
    //! \param[in] t Physical time
    //! \param[in] U Solution vector at recent time step
    //! \param[in,out] R Right-hand side vector computed
    template< class BCType >
    void
    bndIntegralp1( const std::vector< bcconf_t >& bcconfig,
                   const std::map< int, std::vector< std::size_t > >& bface,
                   const std::vector< int >& esuf,
                   const tk::Fields& geoFace,
                   const std::vector< std::size_t >& inpoel,
                   const std::vector< std::size_t >& inpofa,
                   const tk::UnsMesh::Coords& coord,
                   tk::real t,
                   const tk::Fields& U,
                   tk::Fields& R ) const
    {
      for (const auto& s : bcconfig) {       // for all bc sidesets
        auto bc = bface.find( std::stoi(s) );// faces for side set
        if (bc != end(bface))
          bndSurfIntp1< BCType >( bc->second, esuf, geoFace, inpoel, inpofa,
                                  coord, t, U, R );
      }
    }

    //! Riemann solver using upwind method
    //! \param[in] f Face ID
    //! \param[in] gpcoord Coordinates of the flux integration point
    //! \param[in] geoFace Face geometry array
    //! \param[in] u Left and right unknown/state vector
    //! \return Riemann solution using upwind method
    std::vector< tk::real >
    upwindFlux( const std::array< tk::real, 3>& gpcoord,
                std::size_t f,
                const tk::Fields& geoFace,
                const std::array< std::vector< tk::real >, 2 >& u ) const
    {
      std::vector< tk::real > flux( u[0].size(), 0 );

      auto xc = gpcoord[0];
      auto yc = gpcoord[1];
      auto zc = gpcoord[2];

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
      std::array< tk::real, 3 > ba{{ p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2] }},
                                ca{{ p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2] }},
                                da{{ p4[0]-p1[0], p4[1]-p1[1], p4[2]-p1[2] }};
      return tk::triple( ba, ca, da );
    }

    //! Inverse of Jacobian of transformation
    //! \param[in] p1 (x,y,z) coordinates of 1st local node in the tetrahedron
    //! \param[in] p2 (x,y,z) coordinates of 2nd local node in the tetrahedron
    //! \param[in] p3 (x,y,z) coordinates of 3rd local node in the tetrahedron
    //! \param[in] p4 (x,y,z) coordinates of 4th local node in the tetrahedron
    //! \return Inverse of the Jacobian of transformation of physical
    //!   tetrahedron to reference (xi, eta, zeta) space
    std::array< std::array< tk::real, 3 >, 3 >
    getJacInverse( const std::array< tk::real, 3 >& p1,
                   const std::array< tk::real, 3 >& p2,
                   const std::array< tk::real, 3 >& p3,
                   const std::array< tk::real, 3 >& p4 ) const
    {
      std::array< std::array< tk::real, 3 >, 3 > jacInv;

      auto detJ = getJacobian( p1, p2, p3, p4 );

      jacInv[0][0] =  (  (p3[1]-p1[1])*(p4[2]-p1[2])
                       - (p4[1]-p1[1])*(p3[2]-p1[2])) / detJ;
      jacInv[1][0] = -(  (p2[1]-p1[1])*(p4[2]-p1[2])
                       - (p4[1]-p1[1])*(p2[2]-p1[2])) / detJ;
      jacInv[2][0] =  (  (p2[1]-p1[1])*(p3[2]-p1[2])
                       - (p3[1]-p1[1])*(p2[2]-p1[2])) / detJ;

      jacInv[0][1] = -(  (p3[0]-p1[0])*(p4[2]-p1[2])
                       - (p4[0]-p1[0])*(p3[2]-p1[2])) / detJ;
      jacInv[1][1] =  (  (p2[0]-p1[0])*(p4[2]-p1[2])
                       - (p4[0]-p1[0])*(p2[2]-p1[2])) / detJ;
      jacInv[2][1] = -(  (p2[0]-p1[0])*(p3[2]-p1[2])
                       - (p3[0]-p1[0])*(p2[2]-p1[2])) / detJ;

      jacInv[0][2] =  (  (p3[0]-p1[0])*(p4[1]-p1[1])
                       - (p4[0]-p1[0])*(p3[1]-p1[1])) / detJ;
      jacInv[1][2] = -(  (p2[0]-p1[0])*(p4[1]-p1[1])
                       - (p4[0]-p1[0])*(p2[1]-p1[1])) / detJ;
      jacInv[2][2] =  (  (p2[0]-p1[0])*(p3[1]-p1[1])
                       - (p3[0]-p1[0])*(p2[1]-p1[1])) / detJ;

      return jacInv;
    }

};

} // dg::
} // inciter::

#endif // DGTransport_h
