//******************************************************************************
/*!
  \file      src/Base/DataLayout.h
  \author    J. Bakosi
  \date      Fri 19 Feb 2016 09:27:06 AM MST
  \copyright 2012-2016, Jozsef Bakosi.
  \brief     Generic data access abstraction for different data layouts
  \details   Generic data access abstraction for different data layouts. See
    also the rationale discussed in the [design](layout.html) document.
*/
//******************************************************************************
#ifndef DataLayout_h
#define DataLayout_h

#include <array>

#include "Types.h"
#include "Keywords.h"
#include "Exception.h"

namespace tk {

//! Tags for selecting data layout policies
const uint8_t UnkEqComp = 0;
const uint8_t EqCompUnk = 1;

//! Zero-runtime-cost data-layout wrappers with type-based compile-time dispatch
template< uint8_t Layout >
class DataLayout {

  private:
    //! \brief Inherit type of number of components from keyword 'ncomp', used
    //!    also for type of offset
    using ncomp_t = kw::ncomp::info::expect::type;

  public:
    //! Constructor
    //! \param[in] nunk Number of unknowns to allocate memory for
    //! \param[in] nprop Total number of properties, i.e., scalar variables or
    //!   components, per unknown
    //! \author J. Bakosi
    explicit DataLayout( ncomp_t nunk, ncomp_t nprop ) :
      m_vec( nunk*nprop ),
      m_nunk( nunk ),
      m_nprop( nprop ) {}

    //! Const data access dispatch
    //! \details Public interface to const-ref data access to a single real
    //!   value. Use it as DataLayout(p,c,o), where p is the unknown index, c is
    //!   the component index specifying the scalar equation within a system of
    //!   equations, and o is the offset specifying the position at which the
    //!   system resides among other systems. Requirement: offset + component <
    //!   nprop, unknown < nunk, enforced with an assert in DEBUG mode, see also
    //!   the constructor.
    //! \param[in] unknown Unknown index
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return Const reference to data of type tk::real
    //! \author J. Bakosi
    const tk::real&
    operator()( ncomp_t unknown, ncomp_t component, ncomp_t offset ) const
    { return access( unknown, component, offset, int2type< Layout >() ); }

    //! Non-const data access dispatch
    //! \details Public interface to non-const-ref data access to a single real
    //!   value. Use it as DataLayout(p,c,o), where p is the unknown index, c is
    //!   the component index specifying the scalar equation within a system of
    //!   equations, and o is the offset specifying the position at which the
    //!   system resides among other systems. Requirement: offset + component <
    //!   nprop, unknown < nunk, enforced with an assert in DEBUG mode, see also
    //!   the constructor.
    //! \param[in] unknown Unknown index
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return Non-const reference to data of type tk::real
    //! \see "Avoid Duplication in const and Non-const Member Function," and
    //!   "Use const whenever possible," Scott Meyers, Effective C++, 3d ed.
    //! \author J. Bakosi
    tk::real&
    operator()( ncomp_t unknown, ncomp_t component, ncomp_t offset ) {
      return const_cast< tk::real& >(
               static_cast< const DataLayout& >( *this ).
                 operator()( unknown, component, offset ) );
    }

    //! Const ptr to physical variable access dispatch
    //! \details Public interface to the first half of a physical variable
    //!   access. cptr() and var() are two member functions intended to be used
    //!   together in case when component and offset would be expensive to
    //!   compute for data access via the function call operator, i.e., cptr()
    //!   can be used to pre-compute part of the address, which returns a
    //!   pointer and var() can be used to finish the data access using the
    //!   pointer returned by cptr(). In other words, cptr() returns part of the
    //!   address known based on component and offset and intended to be used in
    //!   a setup phase. Then var() takes this partial address and finishes the
    //!   address calculation given the unknown id. Thus the following two data
    //!   accesses are equivalent (modulo constness):
    //!   * real& value = operator()( unk, comp, offs ); and
    //!   * const real* p = cptr( comp, offs ); and
    //!     const real& value = var( p, unk ); or real& value = var( p, unk );
    //!   Requirement: offset + component < nprop, enforced with an assert in
    //!   DEBUG mode, see also the constructor.
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return Pointer to data of type tk::real for use with var()
    //! \see Example client code in Statistics::setupOrdinary() and
    //!   Statistics::accumulateOrd() in Statistics/Statistics.C.
    //! \author J. Bakosi
    const tk::real*
    cptr( ncomp_t component, ncomp_t offset ) const
    { return cptr( component, offset, int2type< Layout >() ); }

    //! Const-ref data-access dispatch
    //! \details Public interface to the second half of a physical variable
    //!   access. cptr() and var() are two member functions intended to be used
    //!   together in case when component and offset would be expensive to
    //!   compute for data access via the function call operator, i.e., cptr()
    //!   can be used to pre-compute part of the address, which returns a
    //!   pointer and var() can be used to finish the data access using the
    //!   pointer returned by cptr(). In other words, cptr() returns part of the
    //!   address known based on component and offset and intended to be used in
    //!   a setup phase. Then var() takes this partial address and finishes the
    //!   address calculation given the unknown id. Thus the following two data
    //!   accesses are equivalent (modulo constness):
    //!   * real& value = operator()( unk, comp, offs ); and
    //!   * const real* p = cptr( comp, offs ); and
    //!     const real& value = var( p, unk ); or real& value = var( p, unk );
    //!   Requirement: unknown < nunk, enforced with an assert in DEBUG mode,
    //!   see also the constructor.
    //! \param[in] pt Pointer to data of type tk::real as returned from cptr()
    //! \param[in] unknown Unknown index
    //! \return Const reference to data of type tk::real
    //! \see Example client code in Statistics::setupOrdinary() and
    //!   Statistics::accumulateOrd() in Statistics/Statistics.C.
    //! \author J. Bakosi
    const tk::real&
    var( const tk::real* pt, ncomp_t unknown ) const
    { return var( pt, unknown, int2type< Layout >() ); }

    //! Non-const-ref data-access dispatch
    //! \details Public interface to the second half of a physical variable
    //!   access. cptr() and var() are two member functions intended to be used
    //!   together in case when component and offset would be expensive to
    //!   compute for data access via the function call operator, i.e., cptr()
    //!   can be used to pre-compute part of the address, which returns a
    //!   pointer and var() can be used to finish the data access using the
    //!   pointer returned by cptr(). In other words, cptr() returns part of the
    //!   address known based on component and offset and intended to be used in
    //!   a setup phase. Then var() takes this partial address and finishes the
    //!   address calculation given the unknown id. Thus the following two data
    //!   accesses are equivalent (modulo constness):
    //!   * real& value = operator()( unk, comp, offs ); and
    //!   * const real* p = cptr( comp, offs ); and
    //!     const real& value = var( p, unk ); or real& value = var( p, unk );
    //!   Requirement: unknown < nunk, enforced with an assert in DEBUG mode,
    //!   see also the constructor.
    //! \param[in] pt Pointer to data of type tk::real as returned from cptr()
    //! \param[in] unknown Unknown index
    //! \return Non-const reference to data of type tk::real
    //! \see Example client code in Statistics::setupOrdinary() and
    //!   Statistics::accumulateOrd() in Statistics/Statistics.C.
    //! \see "Avoid Duplication in const and Non-const Member Function," and
    //!   "Use const whenever possible," Scott Meyers, Effective C++, 3d ed.
    //! \author J. Bakosi
    tk::real&
    var( const tk::real* pt, ncomp_t unknown ) {
      return const_cast< tk::real& >(
               static_cast< const DataLayout& >( *this ).var( pt, unknown ) );
    }

    //! Access to number of unknowns
    //! \return Number of unknowns
    //! \author J. Bakosi
    ncomp_t nunk() const noexcept { return m_nunk; }

    //! Access to number of properties
    //! \details This is the total number of scalar components per unknown
    //! \return Number of propertes/unknown
    //! \author J. Bakosi
    ncomp_t nprop() const noexcept { return m_nprop; }

    //! Extract vector of unknowns given component and offset
    //! \details Requirement: offset + component < nprop, enforced with an
    //!   assert in DEBUG mode, see also the constructor.
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return A vector of unknowns given by component at offset (length:
    //!   nunkn(), i.e., the first constructor argument)
    std::vector< tk::real >
    extract( ncomp_t component, ncomp_t offset ) const {
      std::vector< tk::real > w( m_nunk );
      for (ncomp_t i=0; i<m_nunk; ++i)
        w[i] = operator()( i, component, offset );
      return w;
    }

    //! Extract (a copy of) all components for an unknown
    //! \details Requirement: unknown < nunk, enforced with an assert in DEBUG
    //!   mode, see also the constructor.
    //! \param[in] unknown Index of unknown
    //! \return A vector of components for a single unknown (length: nprop,
    //!   i.e., the second constructor argument)
    std::vector< tk::real >
    extract( ncomp_t unknown ) const {
      std::vector< tk::real > w( m_nprop );
      for (ncomp_t i=0; i<m_nprop; ++i)
        w[i] = operator()( unknown, i, 0 );
      return w;
    }

    //! Extract all components for unknown
    //! \details Requirement: unknown < nunk, enforced with an assert in DEBUG
    //!   mode, see also the constructor.
    //! \param[in] unknown Index of unknown
    //! \return A vector of components for a single unknown (length: nprop,
    //!   i.e., the second constructor argument)
    //! \note This is simply an alias for extract( unknown )
    std::vector< tk::real >
    operator[]( ncomp_t unknown ) const { return extract( unknown ); }

    //! Extract (a copy of) four values of unknowns
    //! \details Requirement: offset + component < nprop, [A,B,C,D] < nunk,
    //!   enforced with an assert in DEBUG mode, see also the constructor.
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \param[in] A Index of 1st unknown
    //! \param[in] B Index of 2nd unknown
    //! \param[in] C Index of 3rd unknown
    //! \param[in] D Index of 4th unknown
    //! \return Array of the four values of component at offset
    //! \author J. Bakosi
    std::array< tk::real, 4 >
    extract( ncomp_t component, ncomp_t offset,
             ncomp_t A, ncomp_t B, ncomp_t C, ncomp_t D ) const
    {
      auto p = cptr( component, offset );
      return {{ var(p,A), var(p,B), var(p,C), var(p,D) }};
    }

    //! Fill vector of unknowns with the same value
    //! \details Requirement: offset + component < nprop, enforced with an
    //!   assert in DEBUG mode, see also the constructor.
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \param[in] value Value to fill vector of unknowns with
    //! \author J. Bakosi
    void fill( ncomp_t component, ncomp_t offset, tk::real value ) {
      auto p = cptr( component, offset );
      for (ncomp_t i=0; i<m_nunk; ++i) var(p,i) = value;
    }

    //! Fill full data storage with value
    //! \param[in] value Value to fill data with
    //! \author J. Bakosi
    void fill( tk::real value )
    { std::fill( begin(m_vec), end(m_vec), value ); }

    //! Layout name dispatch
    //! \return The name of the data layout used
    //! \author J. Bakosi
    static constexpr const char* major()
    { return major( int2type< Layout >() ); }

  private:
    //! Transform a compile-time uint8_t into a type, used for dispatch
    //! \see A. Alexandrescu, Modern C++ Design: Generic Programming and Design
    //!   Patterns Applied, Addison-Wesley Professional, 2001.
    //! \author J. Bakosi
    template< uint8_t m > struct int2type { enum { value = m }; };

    //! Overloads for the various const data accesses
    //! \details Requirement: offset + component < nprop, unknown < nunk,
    //!   enforced with an assert in DEBUG mode, see also the constructor.
    //! \param[in] unknown Unknown index
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return Const reference to data of type tk::real
    //! \see A. Alexandrescu, Modern C++ Design: Generic Programming and Design
    //!   Patterns Applied, Addison-Wesley Professional, 2001.
    //! \author J. Bakosi
    const tk::real&
    access( ncomp_t unknown, ncomp_t component, ncomp_t offset,
            int2type< UnkEqComp > ) const
    {
      Assert( offset + component < m_nprop, "Out-of-bounds access: offset + "
              "component < number of properties" );
      Assert( unknown < m_nunk, "Out-of-bounds access: unknown < number of "
              "unknowns" );
      return m_vec[ unknown*m_nprop + offset + component ];
    }
    const tk::real&
    access( ncomp_t unknown, ncomp_t component, ncomp_t offset,
            int2type< EqCompUnk > ) const
    {
      Assert( offset + component < m_nprop, "Out-of-bounds access: offset + "
              "component < number of properties" );
      Assert( unknown < m_nunk, "Out-of-bounds access: unknown < number of "
              "unknowns" );
      return m_vec[ (offset+component)*m_nunk + unknown ];
    }

    // Overloads for the various const ptr to physical variable accesses
    //! \details Requirement: offset + component < nprop, unknown < nunk,
    //!   enforced with an assert in DEBUG mode, see also the constructor.
    //! \param[in] component Component index, i.e., position of a scalar within
    //!   a system
    //! \param[in] offset System offset specifying the position of the system of
    //!   equations among other systems
    //! \return Pointer to data of type tk::real for use with var()
    //! \see A. Alexandrescu, Modern C++ Design: Generic Programming and Design
    //!   Patterns Applied, Addison-Wesley Professional, 2001.
    //! \author J. Bakosi
    const tk::real*
    cptr( ncomp_t component, ncomp_t offset, int2type< UnkEqComp > ) const {
      Assert( offset + component < m_nprop, "Out-of-bounds access: offset + "
              "component < number of properties" );
      return m_vec.data() + component + offset;
    }
    const tk::real*
    cptr( ncomp_t component, ncomp_t offset, int2type< EqCompUnk > ) const {
      Assert( offset + component < m_nprop, "Out-of-bounds access: offset + "
              "component < number of properties" );
      return m_vec.data() + (offset+component)*m_nunk;
    }

    // Overloads for the various const physical variable accesses
    //!   Requirement: unknown < nunk, enforced with an assert in DEBUG mode,
    //!   see also the constructor.
    //! \param[in] pt Pointer to data of type tk::real as returned from cptr()
    //! \param[in] unknown Unknown index
    //! \return Const reference to data of type tk::real
    //! \see A. Alexandrescu, Modern C++ Design: Generic Programming and Design
    //!   Patterns Applied, Addison-Wesley Professional, 2001.
    //! \author J. Bakosi
    const tk::real&
    var( const tk::real* const pt, ncomp_t unknown, int2type< UnkEqComp > )
    const {
      Assert( unknown < m_nunk, "Out-of-bounds access: unknown < number of "
              "unknowns" );
      return *(pt + unknown*m_nprop);
    }
    const tk::real&
    var( const tk::real* const pt, ncomp_t unknown, int2type< EqCompUnk > )
    const {
      Assert( unknown < m_nunk, "Out-of-bounds access: unknown < number of "
              "unknowns" );
      return *(pt + unknown);
    }

    // Overloads for the name-queries of data lauouts
    //! \return The name of the data layout used
    //! \see A. Alexandrescu, Modern C++ Design: Generic Programming and Design
    //!   Patterns Applied, Addison-Wesley Professional, 2001.
    //! \author J. Bakosi
    static constexpr const char* major( int2type< UnkEqComp > )
    { return "unknown-major"; }
    static constexpr const char* major( int2type< EqCompUnk > )
    { return "equation-major"; }

    std::vector< tk::real > m_vec;      //!< Data pointer
    ncomp_t m_nunk;                     //!< Number of unknowns
    ncomp_t m_nprop;                    //!< Number of properties/unknown
};

} // tk::

#endif // DataLayout_h