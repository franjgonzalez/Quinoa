//******************************************************************************
/*!
  \file      src/Inciter/Partitioner.h
  \author    J. Bakosi
  \date      Tue 22 Dec 2015 10:21:46 PM MST
  \copyright 2012-2015, Jozsef Bakosi.
  \brief     Charm++ chare partitioner group used to perform mesh partitioning
  \details   Charm++ chare partitioner group used to parform mesh partitioning.
*/
//******************************************************************************
#ifndef Partitioner_h
#define Partitioner_h

#include <unordered_map>
#include <numeric>

#include "ExodusIIMeshReader.h"
#include "ContainerUtil.h"
#include "ZoltanInterOp.h"
#include "Inciter/InputDeck/InputDeck.h"

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wconversion"
#endif

#include "partitioner.decl.h"

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

namespace inciter {

extern ctr::InputDeck g_inputdeck;

//! Partitioner Charm++ chare group class
//! \details Instantiations of Partitioner comprise a processor aware Charm++
//!   chare group. When instantiated, a new object is created on each PE and not
//!   more (as opposed to individual chares or chare array object elements). See
//!   also the Charm++ interface file partitioner.ci. The class is templated so
//!   that the same code (parameterized by the template arguments) can be
//!   generated for interacting with different types of Charm++ proxies.
//! \see http://charm.cs.illinois.edu/manuals/html/charm++/manual.html
//! \author J. Bakosi
template< class HostProxy >
class Partitioner : public CBase_Partitioner< HostProxy > {

  // Include Charm++ SDAG code. See http://charm.cs.illinois.edu/manuals/html/
  // charm++/manual.html, Sec. "Structured Control Flow: Structured Dagger".
  Partitioner_SDAG_CODE

  private:
    using Group = CBase_Partitioner< HostProxy >;

  public:
    //! Constructor
    //! \param[in] hostproxy Host Charm++ proxy we are being called from
    Partitioner( HostProxy& host ) :
      m_host( host ),
      m_npe( 0 ),
      m_reordered( 0 )
    {
      tk::ExodusIIMeshReader
        er( g_inputdeck.get< tag::cmd, tag::io, tag::input >() );
      // Read our contiguously-numbered chunk of the mesh graph from file
      readGraph( er );
      // If a geometric partitioner is selected, compute element centroid
      // coordinates
      const auto alg = g_inputdeck.get< tag::selected, tag::partitioner >();
      if ( alg == tk::ctr::PartitioningAlgorithmType::RCB ||
           alg == tk::ctr::PartitioningAlgorithmType::RIB )
        computeCentroids( er );
      else
        signal2host_setup_complete( m_host );
    }

    //! Partition the computational mesh
    //! \param[in] nchare Number of parts the mesh will be partitioned into
    void partition( int nchare ) {
      m_nchare = nchare;
      const auto alg = g_inputdeck.get< tag::selected, tag::partitioner >();
      const auto che = tk::zoltan::geomPartMesh( alg,
                                                 m_centroid,
                                                 m_gelemid,
                                                 m_tetinpoel.size()/4,
                                                 nchare );
      Assert( che.size() == m_gelemid.size(), "Size of ownership array does "
              "not equal the number of mesh graph elements" );
      // Construct global mesh node ids for each chare and distribute
      distribute( chareNodes(che) );
    }

    //! Prepare owned mesh node IDs for reordering
    void flatten() {
      // Make sure we are not fed garbage
      int chunksize, mynchare;
      std::tie( chunksize, mynchare ) = chareDistribution();
      Assert( m_node.size() == mynchare, "Global mesh nodes ids associated "
              "to chares on PE " + std::to_string( CkMyPe() ) + " is "
              "incomplete" );
      // Flatten node IDs of elements our chares operate on
      for (auto& c : m_node)
        m_id.insert( end(m_id), begin(c.second), end(c.second) );
      // Make node ids unique, these need reordering on our PE
      tk::unique( m_id );
      // Call back to host indicating that we are ready for a new node order
      signal2host_flatten_complete( m_host );
      // Send unique global mesh point indices of our chunk to host
      m_host.addNodes( CkMyPe(), m_id );
    }

    //! Reorder global mesh node IDs
    //! \param[in] n Starting node ID we assign new node IDs from
    //! \param[in] comm Communication map used to retrieve node IDs assigned by
    //!   PEs with lower indices than ours
    void reorder( std::size_t n,
                  const std::unordered_map< int,
                          std::set< std::size_t > >& comm )
    {
      // Activate SDAG wait for completing the reordering of our node IDs and
      // for having requests arrive from other PEs for some of our node IDs
      wait4prep();
      // Send out request for new global node IDs for nodes we do not reorder
      for (const auto& c : comm)
        Group::thisProxy[ c.first ].request( CkMyPe(), c.second );
      // Lambda to decide if node ID is being assigned a new ID by us
      auto own = [ &comm ]( std::size_t p ) {
        using Set = std::remove_reference<decltype(comm)>::type::value_type;
        return !std::any_of( cbegin(comm), cend(comm),
                             [&](const Set& s){
                               if (s.second.find(p) != cend(s.second))
                                 return true;
                               else
                                 return false;
                             } );
      };
      // Reorder our chunk of the mesh node IDs by looping through all of our
      // node IDs (resulting from reading our chunk of the mesh cells). We test
      // if we are to assign a new ID to a node ID, and if so, we assign new ID,
      // i.e., reorder, by constructing a map associating new to old IDs. We
      // also count up the reordered nodes.
      for (auto& p : m_id)
        if (own(p)) {           
          m_newid[ p ] = n++;
          ++m_reordered;
        }
      // Trigger SDAG wait, indicating that reordering own node IDs are complete
      trigger_reorderowned_complete();
      // If we have reordered all our nodes, compute and send result to host
      if (m_reordered == m_id.size()) reordered();
    }

    //! Request new global node IDs for old node IDs
    //! \param[in] pe PE request coming from and to which we send new IDs to
    //! \param[in] id Set of old node IDs whose new IDs are requested
    void request( int pe, const std::set< std::size_t >& id ) {
      // Queue up requesting PE and node IDs
      m_req.push_back( { pe, id } );
      // Trigger SDAG wait, signaling that node IDs have been requested from us
      trigger_nodes_requested();
    }

    //! Receive new (reordered) global node IDs
    //! \param[in] id Map associating new to old node IDs
    void neworder( const std::unordered_map< std::size_t, std::size_t >& id ) {
      // Store new node IDs associated to old ones
      for (const auto& p : id) m_newid[ p.first ] = p.second;
      m_reordered += id.size();   // count up number of reordered nodes
      // If we have reordered all our node IDs, send result to host
      if (m_reordered == m_id.size()) reordered();
    }

    //! Compute communication cost of linear system merging for our PE
    //! \param[in] lower Lower global row ID of linear system this PE works on
    //! \param[in] upper Upper global row ID of linear system this PE works on
    //! \param[in] stage Stage of the communication cost estimation
    //! \details  The cost is a real number between 0 and 1, defined as the
    //!   number of mesh points we do not own, i.e., need to send to some other
    //!   PE, divided by the total number of points we contribute to. The lower
    //!   the better.
    void cost( std::size_t lower, std::size_t upper ) {
      std::size_t ownpts = 0, compts = 0;
      for (auto p : m_id) if (p >= lower && p < upper) ++ownpts; else ++compts;
      m_host.costed( CkMyPe(), static_cast<tk::real>(compts) /
                                 static_cast<tk::real>(ownpts + compts) );
    }

    //! Receive mesh node IDs associated to chares we own
    //! \param[in] n Mesh node indices associated to chare IDs
    void add( int frompe,
              const std::unordered_map< int, std::vector< std::size_t > >& n )
    {
      for (const auto& c : n) {
        Assert( pe(c.first) == CkMyPe(), "PE " + std::to_string(CkMyPe()) +
                " received a chareid-nodeidx-vector pair whose chare it does"
                " not own" );
        auto& ch = m_node[ c.first ];
        ch.insert( end(ch), begin(c.second), end(c.second) );
      }
      Group::thisProxy[ frompe ].recv();
    }

    //! Acknowledge received node IDs
    void recv() {
      --m_npe;
      if (recvnodes()) signal2host_distribution_complete( m_host );
    }

  private:
    //! Host proxy
    HostProxy m_host;
    //! Number of fellow PEs to send elem IDs to
    std::size_t m_npe;
    //! Queue of requested node IDs from PEs
    std::vector< std::pair< int, std::set< std::size_t > > > m_req;
    //! Number of mesh nodes reordered
    std::size_t m_reordered;
    //! Tetrtahedron element connectivity of our chunk of the mesh
    std::vector< std::size_t > m_tetinpoel;
    //! Global element IDs we read (our chunk of the mesh)
    std::vector< std::size_t > m_gelemid;
    //! Element centroid coordinates of our chunk of the mesh
    std::array< std::vector< tk::real >, 3 > m_centroid;
    //! Total number of chares across all PEs
    int m_nchare;
    //! \brief Global mesh node ids associated to chares owned
    //! \details Before reordering this map stores (old) global mesh node IDs
    //!   corresponding to the ordering as in the mesh file. After reordering it
    //!   stores the (new) global node IDs the chares contribute to.
    std::unordered_map< int, std::vector< std::size_t > > m_node;
    //! \brief Unique global node IDs chares on our PE will contribute to in a
    //!   linear system
    std::vector< std::size_t > m_id;
    //! \brief Map associating new node IDs (as in producing contiguous-row-id
    //!   linear system contributions) to old node IDs (as in file)
    std::unordered_map< std::size_t, std::size_t > m_newid;

    //! Read our contiguously-numbered chunk of the mesh graph from file
    //! \param[in] er ExodusII mesh reader
    void readGraph( tk::ExodusIIMeshReader& er ) {
      // Get number of mesh points and number of tetrahedron elements in file
      er.readElemBlockIDs();
      auto nel = er.nel( tk::ExoElemType::TET );
      // Read our contiguously-numbered chunk of tetrahedron element
      // connectivity from file and also generate and store the list of global
      // element indices for our chunk of the mesh
      auto chunk = nel / CkNumPes();
      auto from = CkMyPe() * chunk;
      auto till = from + chunk;
      if (CkMyPe() == CkNumPes()-1) till += nel % CkNumPes();
      std::array< std::size_t, 2 > ext = { {static_cast<std::size_t>(from),
                                            static_cast<std::size_t>(till-1)} };
      er.readElements( ext, tk::ExoElemType::TET, m_tetinpoel );
      m_gelemid.resize( static_cast<std::size_t>(till-from) );
      std::iota( begin(m_gelemid), end(m_gelemid), from );
      signal2host_graph_complete( m_host, m_gelemid.size() );
    }

    // Compute element centroid coordinates
    //! \param[in] er ExodusII mesh reader
    void computeCentroids( tk::ExodusIIMeshReader& er) {
      // Construct unique global mesh point indices of our chunk
      auto gid = m_tetinpoel;
      tk::unique( gid );
      // Read node coordinates of our chunk of the mesh elements from file
      auto coord = er.readNodes( tk::extents(gid) );
      // Make room for element centroid coordinates
      auto& cx = m_centroid[0];
      auto& cy = m_centroid[1];
      auto& cz = m_centroid[2];
      auto num = m_tetinpoel.size()/4;
      cx.resize( num );
      cy.resize( num );
      cz.resize( num );
      // Compute element centroids for our chunk of the mesh elements
      for (std::size_t e=0; e<num; ++e) {
        const auto& a = tk::cref_find( coord, m_tetinpoel[e*4+0] );
        const auto& b = tk::cref_find( coord, m_tetinpoel[e*4+1] );
        const auto& c = tk::cref_find( coord, m_tetinpoel[e*4+2] );
        const auto& d = tk::cref_find( coord, m_tetinpoel[e*4+3] );
        cx[e] = (a[0] + b[0] + c[0] + d[0]) / 4.0;
        cy[e] = (a[1] + b[1] + c[1] + d[1]) / 4.0;
        cz[e] = (a[2] + b[2] + c[2] + d[2]) / 4.0;
      }
      signal2host_setup_complete( m_host );
    }

    //! Construct global mesh node ids for each chare
    //! \param[in] che Chares of elements: array of chare ownership IDs mapping
    //!   graph elements to Charm++ chares. Size: number of elements in the
    //!   chunk of the mesh graph on this PE.
    //! \return Vector of global mesh node ids connecting elements owned by each
    //!   chare on this PE
    //! \note The chare IDs, as keys in the map constructed here, are simply the
    //!   chare IDs returned by the partitioner assigning mesh elements to these
    //!   chares. It does not mean that these chare IDs are owned on this PE.
    std::unordered_map< int, std::vector< std::size_t > >
    chareNodes( const std::vector< std::size_t >& che ) const
    {
      Assert( che.size() == m_gelemid.size(), "The size of the global element "
              "index and the chare element arrays must equal" );
      Assert( che.size() == m_tetinpoel.size()/4, "The size of the mesh "
              "connectivity / 4 and the chare element arrays must equal" );
      // Categorize global mesh node ids of elements by chares
      std::unordered_map< int, std::vector< std::size_t > > nodes;
      for (std::size_t e=0; e<che.size(); ++e) {
        auto& c = nodes[ static_cast<int>(che[e]) ];
        for (std::size_t n=0; n<4; ++n) c.push_back( m_tetinpoel[e*4+n] );
      }
      // Make sure all PEs have chares assigned
      Assert( !nodes.empty(), "No nodes have been assigned to chares on PE " +
              std::to_string(CkMyPe()) );
      // This check should always be done, hence ErrChk and not Assert, as it
      // can result from particular pathological combinations of (1) too large
      // degree of virtualization, (2) too many PEs, and/or (3) too small of a
      // mesh and not due to programmer error.
      for(const auto& c : nodes)
        ErrChk( !c.second.empty(),
                "Overdecomposition of the mesh is too large compared to the "
                "number of work units computed based on the degree of "
                "virtualization desired. As a result, there would be at least "
                "one work unit with no mesh elements to work on, i.e., nothing "
                "to do. Solution 1: decrease the virtualization to a lower "
                "value using the command-line argument '-u'. Solution 2: "
                "decrease the number processing elements (PEs) using the "
                "charmrun command-line argument '+pN' where N is the number of "
                "PEs, which implicitly increases the size (and thus decreases "
                "the number) of work units.)" );
      return nodes;
    }

    //! Distribute global mesh node IDs to their owner PEs
    //! \param[in] n Global mesh node IDs connecting elements associated to
    //!   chare IDs on this PE resulting from partitioning the mesh elements.
    //!   Note that this data is moved in.
    //! \details Chare ids are distributed to PEs in a linear continguous order
    //!   with the last PE taking the remainder if the number of PEs is not
    //!   divisible by the number chares. For example, if nchare=7 and npe=3,
    //!   the chare distribution is PE0: 0 1, PE1: 2 3, and PE2: 4 5 6. As a
    //!   result of this distribution, all PEs will have their m_node map filled
    //!   with the global mesh node IDs associated to the Charm++ chare IDs each
    //!   PE owns.
    void distribute( std::unordered_map< int, std::vector< std::size_t > >&& n )
    {
      int chunksize, mynchare;
      std::tie( chunksize, mynchare ) = chareDistribution();
      for (int c=0; c<mynchare; ++c) {
        auto chid = CkMyPe() * chunksize + c; // compute owned chare ID
        const auto it = n.find( chid );       // attempt to find its nodes
        if (it != end(n)) {                   // if found
          m_node.insert( *it );               // move over owned key-value pairs
          n.erase( it );                      // remove chare ID and nodes
        }
        Assert( n.find(chid) == end(n), "Not all owned node IDs stored" );
      }
      // Construct export map associating those map entries (mesh node indices
      // associated to chare IDs) owned by chares we do not own. Outer key: PE
      // to export to, inner key: chare ID, value: vector of global node IDs
      std::unordered_map< int,
        std::unordered_map< int, std::vector< std::size_t > > > exp;
      for (auto&& c : n) exp[ pe(c.first) ].insert( std::move(c) );
      // Export chare IDs and node IDs we do not own to fellow PEs
      m_npe = exp.size();
      for (const auto& p : exp)
        Group::thisProxy[ p.first ].add( CkMyPe(), p.second );
      if (recvnodes()) signal2host_distribution_complete( m_host );
    }

    //! Compute chare distribution
    //! \return Chunksize, i.e., number of chares per all PEs except the last
    //!   one, and the number of chares for my PE
    //! \details This computes a simple contiguous chare distribution across
    //!   PEs.
    std::pair< int, int > chareDistribution() const {
      auto chunksize = m_nchare / CkNumPes();
      auto mynchare = chunksize;
      if (CkMyPe() == CkNumPes()-1) mynchare += m_nchare % CkNumPes();
      return { chunksize, mynchare };
    }

    //! Test if all fellow PEs have received my node IDs contributions
    //! \return True if all fellow PEs have received what I have sent them
    bool recvnodes() const { return m_npe == 0; }

    //! Return processing element for chare id
    //! \param[in] id Chare id
    //! \return PE that creates the chare
    //! \details This is computed based on a simple contiguous linear
    //!   distribution of chare ids to PEs.
    int pe( int id ) const {
      auto p = id / (m_nchare / CkNumPes());
      if (p >= CkNumPes()) p = CkNumPes()-1;
      Assert( p < CkNumPes(), "Assigning to nonexistent PE" );
      return p;
    }

    //! Associate new node IDs to old ones and return them to the requestor(s)
    void prepare() {
      for (const auto& r : m_req) {
        std::unordered_map< std::size_t, std::size_t > n;
        for (auto p : r.second) n[ p ] = tk::val_find( m_newid, p );
        Group::thisProxy[ r.first ].neworder( n );
        n.clear();
      }
      m_req.clear();    // Clear queue of requests just fulfilled
      wait4prep();      // Re-enable SDAG wait for preparing new node requests
      // Re-enable trigger signaling that reordering of owned node IDs are
      // complete right away
      trigger_reorderowned_complete();
    }

    //! Compute final result of reordering and send it back to host
    //! \details This member function is called when both those node IDs that we
    //!   assign a new ordering to as well as those assigned new IDs by other
    //!   PEs have been reordered (and we contribute to) and we are ready (on
    //!   this PE) to compute our final result of the reordering and send it
    //!   back to the host.
    void reordered() {
      // Construct maps associating old node IDs (as in file) to new node IDs
      // (as in producing contiguous-row-id linear system contributions)
      // associated to chare IDs (outer key). This is basically the inverse of
      // m_newid and categorized by chares. Note that m_node at this point still
      // contains the old global node IDs the chares contribute to.
      std::unordered_map< int,
        std::unordered_map< std::size_t, std::size_t > > chcid;
      for (const auto& c : m_node) {
        auto& m = chcid[ c.first ];
        for (auto p : c.second) m[ tk::val_find(m_newid,p) ] = p;
      }
      // Update our chare ID maps to now contain the new global node IDs
      // instead of the old ones
      for (auto& c : m_node)
        for (auto& p : c.second)
          p = tk::val_find( m_newid, p );
      // Send back result to host
      m_host.prepared( CkMyPe(), m_node, chcid );
      // Update unique global node IDs of chares our PE will contribute to the
      // new IDs resulting from reordering
      for (auto& p : m_id) p = tk::val_find( m_newid, p );
    }

    //! Signal back to host that we have done our part of reading the mesh graph
    void signal2host_graph_complete( const CProxy_Conductor& host,
                                     uint64_t nelem )
    {
      Group::contribute(sizeof(uint64_t), &nelem, CkReduction::sum_int,
                        CkCallback( CkReductionTarget(Conductor,load), host ));
    }
    //! Signal back to host that we are ready for partitioning the mesh
    void signal2host_setup_complete( const CProxy_Conductor& host ) {
      Group::contribute(
        CkCallback(CkIndex_Conductor::redn_wrapper_partition(NULL), m_host ));
    }
    //! \brief Signal back to host that we have done our part of distributing
    //!   mesh node IDs after partitioning
    void signal2host_distribution_complete( const CProxy_Conductor& host ) {
      Group::contribute(
        CkCallback( CkIndex_Conductor::redn_wrapper_flatten(NULL), m_host ) );
    }
    //! Signal back to host that we are ready for a new mesh node order
    void signal2host_flatten_complete( const CProxy_Conductor& host ) {
      Group::contribute(
        CkCallback(CkIndex_Conductor::redn_wrapper_flattened(NULL), m_host ));
    }
};

} // inciter::

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wconversion"
#endif

#define CK_TEMPLATES_ONLY
#include "partitioner.def.h"
#undef CK_TEMPLATES_ONLY

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

#endif // Partitioner_h
