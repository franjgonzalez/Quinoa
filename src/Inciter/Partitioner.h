// *****************************************************************************
/*!
  \file      src/Inciter/Partitioner.h
  \author    J. Bakosi
  \date      Fri 03 Mar 2017 06:37:25 PM MST
  \copyright 2012-2015, Jozsef Bakosi, 2016, Los Alamos National Security, LLC.
  \brief     Charm++ chare partitioner group used to perform mesh partitioning
  \details   Charm++ chare partitioner group used to perform mesh partitioning.
    The implementation uses the Charm++ runtime system and is fully
    asynchronous, overlapping computation, communication as well as I/O. The
    algorithm utilizes the structured dagger (SDAG) Charm++ functionality. The
    high-level overview of the algorithm structure and how it interfaces with
    Charm++ is discussed in the Charm++ interface file
    src/Inciter/partitioner.ci.

    #### Call graph ####
    The following is a directed acyclic graph (DAG) that outlines the
    asynchronous algorithm implemented in this class The detailed discussion of
    the algorithm is given in the Charm++ interface file partitioner.ci,
    which also repeats the graph below using ASCII graphics. On the DAG orange
    fills denote global synchronization points, orange frames with white fill
    are partial synchronization points that overlap with other tasks, and dashed
    lines are potential shortcuts that allow jumping over some of the task-graph
    under some circumstances. See the detailed discussion in partitioner.ci.
    \dot
    digraph "Partitioner SDAG" {
      rankdir="LR";
      node [shape=record, fontname=Helvetica, fontsize=10];
      Own [ label="Own" tooltip="owned nodes reordered"
             URL="\ref inciter::Partitioner::reorder"];
      Req [ label="Req" tooltip="nodes requested"
             URL="\ref inciter::Partitioner::request"];
      Pre [ label="Pre" tooltip="start preparing node IDs"
            URL="\ref inciter::Partitioner::prepare" color="#e6851c"];
      Ord [ label="Ord" tooltip="Node IDs reordered"
            URL="\ref inciter::Partitioner::reordered" color="#e6851c"];
      Low [ label="Low" tooltip="lower bound received"
             URL="\ref inciter::Partitioner::lower"];
      Upp [ label="Upp" tooltip="upper bound computed"
             URL="\ref inciter::Partitioner::bounds"];
      Par [ label="Par" tooltip="partitioners participated"
             URL="\ref inciter::Partitioner::neworder"];
      Cre [ label="Cre" tooltip="create workers"
             URL="\ref inciter::Partitioner::create" color="#e6851c"];
      Own -> Pre [ style="solid" ];
      Req -> Pre [ style="solid" ];
      Pre -> Ord [ style="solid" ];
      Ord -> Low [ style="solid" ];
      Ord -> Upp [ style="solid" ];
      Ord -> Par [ style="solid" ];
      Low -> Cre [ style="solid" ];
      Upp -> Cre [ style="solid" ];
      Par -> Cre [ style="solid" ];
    }
    \enddot
    \include Inciter/partitioner.ci
*/
// *****************************************************************************
#ifndef Partitioner_h
#define Partitioner_h

#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <numeric>

#include "ExodusIIMeshReader.h"
#include "ExodusIIMeshWriter.h" // NOT NEEDED!
#include "ContainerUtil.h"
#include "ZoltanInterOp.h"
#include "Inciter/InputDeck/InputDeck.h"
#include "Options/PartitioningAlgorithm.h"
#include "LinSysMerger.h"
#include "DerivedData.h"
#include "UnsMesh.h"

namespace inciter {

extern ctr::InputDeck g_inputdeck;
extern CkReduction::reducerType NodesMerger;

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wundefined-func-template"
#endif

//! Partitioner Charm++ chare group class
//! \details Instantiations of Partitioner comprise a processor aware Charm++
//!   chare group. When instantiated, a new object is created on each PE and not
//!   more (as opposed to individual chares or chare array object elements). See
//!   also the Charm++ interface file partitioner.ci.
//! \author J. Bakosi
template< class HostProxy, class WorkerProxy, class LinSysMergerProxy,
          class ParticleWriterProxy >
class Partitioner : public CBase_Partitioner< HostProxy,
                                              WorkerProxy,
                                              LinSysMergerProxy,
                                              ParticleWriterProxy > {

  #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-parameter"
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
  #elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  #elif defined(__INTEL_COMPILER)
    #pragma warning( push )
    #pragma warning( disable: 1478 )
  #endif
  // Include Charm++ SDAG code. See http://charm.cs.illinois.edu/manuals/html/
  // charm++/manual.html, Sec. "Structured Control Flow: Structured Dagger".
  Partitioner_SDAG_CODE
  #if defined(__clang__)
    #pragma clang diagnostic pop
  #elif defined(__GNUC__)
    #pragma GCC diagnostic pop
  #elif defined(__INTEL_COMPILER)
    #pragma warning( pop )
  #endif

  private:
    using Group = CBase_Partitioner< HostProxy, WorkerProxy, LinSysMergerProxy,
                                     ParticleWriterProxy >;

  public:
    //! Constructor
    //! \param[in] host Host Charm++ proxy we are being called from
    //! \param[in] worker Worker Charm++ proxy we spawn PDE work to
    //! \param[in] lsm Linear system merger proxy (required by the workers)
    Partitioner( const HostProxy& host,
                 const WorkerProxy& worker,
                 const LinSysMergerProxy& lsm,
                 const ParticleWriterProxy& pw ) :
      __dep(),
      m_host( host ),
      m_worker( worker ),
      m_linsysmerger( lsm ),
      m_particlewriter( pw ),
      m_npe( 0 ),
      m_req(),
      m_reqed(),
      m_start( 0 ),
      m_noffset( 0 ),
      m_nquery( 0 ),
      m_nshed( 0 ),
      m_coord(),
      m_tetinpoel(),
      m_gelemid(),
      m_centroid(),
      m_nchare( 0 ),
      m_lower( 0 ),
      m_upper( 0 ),
      m_node(),
      m_ncomm(),
      m_ecomm(),
      m_ncommunication(),
      m_ecommunication(),
      m_nd(),
      m_ed(),
      m_newnd(),
      m_newed(),
      m_chnodemap(),
      m_chedgenodes(),
      m_cost( 0.0 ),
      m_cn(),
      m_msum()
    {
      tk::ExodusIIMeshReader
        er( g_inputdeck.get< tag::cmd, tag::io, tag::input >() );
      // Read our contiguously-numbered chunk of the mesh graph from file
      readGraph( er );
      // If a geometric partitioner is selected, compute element centroid
      // coordinates
      const auto alg = g_inputdeck.get< tag::selected, tag::partitioner >();
      if ( tk::ctr::PartitioningAlgorithm().geometric(alg) )
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
      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.pepartitioned();
      Assert( che.size() == m_gelemid.size(), "Size of ownership array does "
              "not equal the number of mesh graph elements" );
      // Construct global mesh node ids for each chare and distribute
      distribute( chareNodes(che) );
      // Free storage of element connectivity, element centroids, and element
      // IDs as they are no longer needed after the mesh partitioning.
      tk::destroy( m_gelemid );
      tk::destroy( m_centroid );
    }

    //! Receive number of uniquely assigned global mesh node IDs from lower PEs
    //! \details This function computes the offset each PE will need to start
    //!   assigning its new node IDs from (for those nodes that are not assigned
    //!   new IDs by any PEs with lower indices). The offset for a PE is the
    //!   offset for the previous PE plus the number of node IDs the previous PE
    //!   (uniquely) assigns new IDs for minus the number of node IDs the
    //!   previous PE receives from others (lower PEs). This is computed here in
    //!   a parallel/distributed fashion by each PE sending its number of node
    //!   IDs (that it uniquely assigns) to all PEs. Note that each PE would
    //!   only need to send this information to higher PEs, but instead this
    //!   function is called in a broadcast fashion, because that is more
    //!   efficient than individual calls to only the higher PEs. Therefore when
    //!   computing the offsets, we only count the lower PEs. When this is done,
    //!   we have the precise communication map as well as the start offset on
    //!   all PEs and so we can start the distributed global mesh node ID
    //!   reordering.
    void offset( int p, std::size_t u ) {
      if (p < CkMyPe()) m_start += u;
      if (++m_noffset == static_cast<std::size_t>(CkNumPes())) reorder();
    }

    //! Request new global node IDs for old node IDs
    //! \param[in] p PE request coming from and to which we send new IDs to
    //! \param[in] nd Set of old node IDs whose new IDs are requested
    void request( int p, const std::unordered_set< std::size_t >& nd ) {
      // Queue up requesting PE and node IDs
      m_req.push_back( { p, nd } );
      // Trigger SDAG wait signaling that node IDs have been requested from us
      nodes_requested_complete();
    }

    //! Request new global node IDs for edges
    //! \param[in] p PE request coming from and to which we send new IDs to
    //! \param[in] ed Set of edges whose new IDs are requested
    void request( int p, const tk::UnsMesh::Edges& ed ) {
      // Queue up requesting PE and node IDs
      m_reqed.push_back( { p, ed } );
      // Trigger SDAG wait signaling that node IDs have been requested from us
      nodes_requested_complete();
    }

    //! \brief Receive shared edges and associated node IDs added during uniform
    //!   initial refinement
    //! \param[in] p PE the shared edges coming from
    //! \param[in] sh List of edges (given by a pair of global node IDs) to which
    //!   nodes have been added and associated new node IDs
    void sharededges( int p, const tk::UnsMesh::EdgeNodes& sh,
                      const tk::UnsMesh::EdgeNodeCoords& shc ) {
IGNORE(p);
IGNORE(sh);
IGNORE(shc);
// //std::cout << "\nsh: " << CkMyPe() << '<' << p << ',' << sh.size() << ": ";
//       for (const auto& s : sh)
//         for (auto& c : m_chedgenodes) {
//           auto e = c.second.find( s.first );
//           if (e != end(c.second) && p < CkMyPe()) {
//             auto oldpe = m_edgePEs.find( s.first );
//             if ( oldpe == end(m_edgePEs) ||
//                 (oldpe != end(m_edgePEs) && p < oldpe->second) )
//             {
// //std::cout << "\nsh: " << CkMyPe() << '<' << p << ',' << " r:" << s.first[0] << '-' << s.first[1] << ':' << e->second << "->" << s.second << ' ';
//               m_edgePEs[ s.first ] = p;  // associate lowest owner PE to edge
//               m_sharedEdgeNodes[ e->second ] = s.second;
//               auto& x = m_coord[0];
//               auto& y = m_coord[1];
//               auto& z = m_coord[2];
//               auto i = s.first[0];
//               auto j = s.first[1];
//               auto inc = tk::cref_find( shc, s.first );
//               if (std::abs(inc[0]-(x[i]+x[j])/2.0) > 1.0e-5 ||
//                   std::abs(inc[1]-(y[i]+y[j])/2.0) > 1.0e-5 ||
//                   std::abs(inc[2]-(z[i]+z[j])/2.0) > 1.0e-5)
// std::cout << "\nsh: " << CkMyPe() << '<' << p << ',' << " would-be-generated edge-coords disagree: " << i << '-' << j << ": (" << e->second << "->" << s.second << "), coords: " << inc[0] << ", " << inc[1] << ", " << inc[2] << " ne " << (x[i]+x[j])/2.0 << ", " << (y[i]+y[j])/2.0 << ", " << (z[i]+z[j])/2.0 << '\n';
// //                else
// // std::cout << "\nsh: " << CkMyPe() << '<' << p << ',' << " edge-coords agree r:" << s.first[0] << '-' << s.first[1] << ':' << e->second << "->" << s.second << ' ';
//               if (std::abs(inc[0]-x[e->second]) > 1.0e-5 ||
//                   std::abs(inc[1]-y[e->second]) > 1.0e-5 ||
//                   std::abs(inc[2]-z[e->second]) > 1.0e-5)
// std::cout << "\nsh: " << CkMyPe() << '<' << p << ',' << " looked-up edge-coords disagree: " << i << '-' << j << ": (" << e->second << "->" << s.second << "), coords: " << inc[0] << ", " << inc[1] << ", " << inc[2] << " ne " << x[e->second] << ", " << y[e->second] << ", " << z[e->second] << '\n';
//             }
//           }
//         }
// 
//       if (++m_nshed == m_msum.size()) {
//         // update node ID in m_chedgenodes
//         for (auto& c : m_chedgenodes) {
//           decltype(c.second) r;
//           for (const auto& n : c.second) {
//             auto it = m_sharedEdgeNodes.find( n.second );
//             if (it != end(m_sharedEdgeNodes))
//               r[ n.first ] = it->second;
//             else
//               r[ n.first ] = n.second;
//           }
//           c.second = std::move( r );
//         }
//         // update node ID in m_node
//         for (auto& c : m_node) {
//           //decltype(c.second) r( c.second.size() );
//           auto r = c.second;
//           std::size_t i = 0;
//           for (auto n : c.second) {
//             auto it = m_sharedEdgeNodes.find( n );
//             if (it != end(m_sharedEdgeNodes))
//               r[i] = it->second;
//             //else
//               //r[i] = n;
//             ++i;
//           }
//           c.second = std::move( r );
//         }
//         // update node ID in m_msum
//         for (auto& c : m_msum)
//           for (auto& s : c.second) {
//             decltype(s.second) r;
//             for (const auto& n : s.second) {
//               auto it = m_sharedEdgeNodes.find( n );
//               if (it != end(m_sharedEdgeNodes))
//                 r.insert( it->second );
//               else
//                 r.insert( n );
//             }
//             s.second = std::move( r );
//           }
//         // update node coordinates
//         auto nc = m_coord;
//         for (const auto& n : m_sharedEdgeNodes) {
//           nc[0][ n.second ] = m_coord[0][ n.first ];
//           nc[1][ n.second ] = m_coord[1][ n.first ];
//           nc[2][ n.second ] = m_coord[2][ n.first ];
//         }
//         m_coord = std::move( nc );
//         matched();
//       }
    }

    //! Receive new (reordered) global node IDs
    //! \param[in] nd Map associating new to old node IDs
    void neworder( const std::unordered_map< std::size_t, std::size_t >& nd ) {
      // Signal to the runtime system that we have participated in reordering
      participated_complete();
      // Store new node IDs associated to old ones
      for (const auto& p : nd) m_newnd[ p.first ] = p.second;
      // If all our nodes have new IDs assigned, signal that to the runtime
      if (m_newnd.size() == m_nd.size()) nodesreorder_complete();
    }

    //! Receive new global node IDs associated to edge-nodes
    //! \param[in] ed Map associating node IDs to edges
    void neworder( const tk::UnsMesh::EdgeNodes& ed ) {
      // Signal to the runtime system that we have participated in reordering
      participated_complete();
      // Store node IDs associated to edge
      for (const auto& e : ed) m_newed[ e.first ] = e.second;
      // If all our edges have new IDs assigned, signal that to the runtime
      if (m_newed.size() == m_ed.size()) edgesreorder_complete();
    }

    //! Receive mesh node IDs associated to chares we own
    //! \param[in] n Mesh node indices associated to chare IDs
    //! \param[in] frompe PE call coming from
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
    void recv() { if (--m_npe == 0) signal2host_distributed( m_host ); }

    //! Prepare owned mesh node IDs for reordering
    void flatten() {

      refine();

      // Make sure we are not fed garbage
      Assert( m_node.size() == static_cast<std::size_t>(chareDistribution()[1]),
              "Global mesh nodes ids associated to chares on PE " +
              std::to_string( CkMyPe() ) + " is incomplete" );
      // Collect chare IDs we own associated to old global mesh node IDs
      for (const auto& c : m_node)
        for (auto p : c.second)
          m_cn[p].push_back( c.first );
      // Make chare IDs (associated to old global mesh node IDs) unique
      for (auto& c : m_cn) tk::unique( c.second );
      // Flatten node IDs of elements our chares operate on
      for (const auto& c : m_node)
        for (auto i : c.second)
          m_nd.insert( i );
      // Flatten edges of elements our chares operate on
      for (const auto& c : m_chedgenodes)
        for (const auto& e : c.second)
          m_ed.insert( e.first );
      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.peflattened();
      // Signal host that we are ready for computing the communication map,
      // required for parallel distributed global mesh node reordering
      signal2host_flattened( m_host );
    }

    //! Receive lower bound of node IDs our PE operates on after reordering
    //! \param[in] low Lower bound of node IDs assigned to us
    void lower( std::size_t low ) {
      m_lower = low;
      lower_complete();
    }

    //! \brief Compute the variance of the communication cost of merging the
    //!   linear system
    //! \param[in] av Average of the communication cost
    //! \details Computing the standard deviation is done via computing and
    //!   summing up the variances on each PE and asynchronously reducing the
    //!   sum to our host.
    void stdCost( tk::real av )
    { signal2host_stdcost( m_host, (m_cost-av)*(m_cost-av) ); }

    //! \brief Start gathering global node IDs this PE will need to receive
    //!   (instead of assign) during reordering
    void gather() { Group::thisProxy.query( CkMyPe(), m_nd, m_edgenodes ); }

    //! \brief Query our global node IDs by other PEs so they know if they
    //!   receive IDs for those from during reordering
    //! \param[in] p Querying PE
    //! \param[in] id Vector of global mesh node IDs to query
    //! \details Note that every PE calls this function in a broadcast fashion,
    //!   including our own. However, to compute the correct result, this would
    //!   only be necessary for PEs whose ID is higher than ours. However, the
    //!   broadcast (calling everyone) is more efficient. This also results in a
    //!   simpler logic, because every PE goes through this single call path.
    //!   The returned mask is simply a boolean array signaling if the node ID
    //!   is found (owned).
    void query( int p,
                const std::set< std::size_t >& id,
                const tk::UnsMesh::EdgeNodes& ed ) const
    {
      std::unordered_map< std::size_t, std::vector< int > > cn;
      for (auto j : id) {
        const auto it = m_nd.find( j );
        if (it != end(m_nd)) {
          const auto& c = tk::cref_find( m_cn, j );
          auto& chares = cn[j];
          chares.insert( end(chares), begin(c), end(c) );
        }
      }
      tk::UnsMesh::EdgeNodes ce;
      for (const auto& j : ed) {
        const auto it = m_edgenodes.find( j.first );
        if (it != end(m_edgenodes)) ce[ j.first ] = it->second;
      }
      Group::thisProxy[ p ].mask( CkMyPe(), cn, ce );
    }

    //! Receive mask of to-be-received global mesh node IDs
    //! \param[in] p The PE uniquely assigns the node IDs marked listed in ch
    //! \param[in] ch Vector containing the set of potentially multiple chare
    //!   IDs that we own (i.e., contribute to) for all of our node IDs.
    //! \details Note that every PE will call this function, since query() was
    //!   called in a broadcast fashion and query() answers to every PE once.
    //!   This is more efficient than calling only the PEs from which we would
    //!   have to receive results from. Thus the incoming results are only
    //!   interesting from PEs with lower IDs than ours.
    void mask( int p,
               const std::unordered_map< std::size_t, std::vector< int > >& ch,
               const tk::UnsMesh::EdgeNodes& ce )
    {
      // Store the old global mesh node IDs associated to chare IDs bordering
      // the mesh chunk held by and associated to chare IDs we own
      for (const auto& h : ch) {
        const auto& chares = tk::ref_find( m_cn, h.first );
        for (auto c : chares) {           // surrounded chares
          auto& sch = m_msum[c];
          for (auto s : h.second)         // surrounding chares
            if (s != c) sch[ s ].insert( h.first );
        }
      }
      // Associate global mesh node IDs to lower PEs we will need to receive
      // from during node reordering. The choice of associated container is
      // std::map, which is ordered (vs. unordered, hash-map). This is required
      // by the following operation that makes the mesh node IDs unique in the
      // communication map. (We are called in an unordered fashion, so we need
      // to collect from all PEs and then we need to make the node IDs unique,
      // keeping only the lowest PEs a node ID is associated with.)
      if (p < CkMyPe()) {
        auto& id = m_ncomm[ p ];
        for (const auto& h : ch) id.insert( h.first );
        for (const auto& h : ce) {
          Assert( m_edgenodes.find(h.first) != end(m_edgenodes),
                  std::to_string(CkMyPe()) + " received and edge from PE " +
                  std::to_string(p) + " it does not have" );
          m_ecomm[ p ].insert( h.first );
        }
      }
      if (++m_nquery == static_cast<std::size_t>(CkNumPes())) {
        // Make sure we have received all we need
        Assert( m_ncomm.size() == static_cast<std::size_t>(CkMyPe()),
                "Communication map size on PE " +
                std::to_string(CkMyPe()) + " must equal " +
                std::to_string(CkMyPe()) );
        // Fill new hash-map, keeping only unique node IDs obtained from the
        // lowest possible PEs
        for (auto c=m_ncomm.cbegin(); c!=m_ncomm.cend(); ++c) {
          auto& n = m_ncommunication[ c->first ];
          for (auto j : c->second)
            if (std::none_of( m_ncomm.cbegin(), c,
                 [ j ]( const typename decltype(m_ncomm)::value_type& s )
                 { return s.second.find(j) != end(s.second); } )) {
              n.insert(j);
            }
          if (n.empty()) m_ncommunication.erase( c->first );
        }
        for (auto c=m_ecomm.cbegin(); c!=m_ecomm.cend(); ++c) {
          auto& e = m_ecommunication[ c->first ];
          for (const auto& j : c->second)
            if (std::none_of( m_ecomm.cbegin(), c,
                 [ &j ]( const typename decltype(m_ecomm)::value_type& s )
                 { return s.second.find(j) != end(s.second); } )) {
              e.insert(j);
            }
          if (e.empty()) m_ecommunication.erase( c->first );
        }
        // Free storage of temporary communication map used to receive global
        // mesh node IDs as it is no longer needed once the final communication
        // map is generated.
        tk::destroy( m_ncomm );
        // Count up total number of nodes and (nodes associated to edges) we
        // will need receive during reordering
        std::size_t nrecv = 0, erecv = 0;
        for (const auto& u : m_ncommunication) nrecv += u.second.size();
        for (const auto& e : m_ecommunication) erecv += e.second.size();
        // send progress report to host
        if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
          m_host.pemask();
        // Start computing PE offsets for node reordering
        Group::thisProxy.offset( CkMyPe(),
                                 m_nd.size()-nrecv + m_ed.size()-erecv );
      }
    }

    void edges() {
      matched();
// std::cout << CkMyPe() << '>';
// for (const auto& c : m_ncommunication) {
//   std::cout << c.first << ": ";
//   for (auto i : c.second) std::cout << i << ' ';
// }
// std::cout << '\n';

// std::cout << '\n' << CkMyPe() << " chedgenodes: ";
// for (const auto& c : m_chedgenodes) {
//   std::cout << c.first << ": ";
//   for (const auto& n : c.second) std::cout << n.first[0] << '-' << n.first[1] << ':' << n.second << ' ';
// }
// 
// std::cout << '\n' << CkMyPe() << " edges>";
// for (const auto& c : m_msum) {
//   std::cout << c.first << ": ";
//   for (const auto& n : c.second) {
//     std::cout << n.first << '>';
//     for (auto i : n.second) std::cout << i << ' ';
//   }
// }
// 
//       auto dist = chareDistribution();
//       // lambda to find out if both edge-endpoints are shared by a fellow PE
//       auto shared = [ this, &dist ]( const tk::UnsMesh::Edge& ed ) -> int {
//         for (const auto& c : m_msum)
//           for (const auto& n : c.second)
//             if ( n.second.find(ed[0]) != end(n.second) &&
//                  n.second.find(ed[1]) != end(n.second) )
//               return n.first/dist[0];  // return PE that shares the edge
//         return -1;
//       };
//       // Collect edges shared by chares on other PEs
//       std::unordered_map< int, tk::UnsMesh::EdgeNodes > sh;
//       std::unordered_map< int, tk::UnsMesh::EdgeNodeCoords > shc;
//       auto& x = m_coord[0];
//       auto& y = m_coord[1];
//       auto& z = m_coord[2];
//       for (const auto& c : m_chedgenodes)
//         for (const auto& n : c.second) {
//           auto p = shared( n.first );
//           if (p != -1 && p != CkMyPe()) {
//             sh[ p ][ n.first ] = n.second;
//             auto i = n.first[0];
//             auto j = n.first[1];
//             shc[ p ][ n.first ] = {{ (x[i]+x[j])/2.0,
//                                      (y[i]+y[j])/2.0,
//                                      (z[i]+z[j])/2.0 }};
//           }
//         }
// 
//       // Send shared edge-nodes and associated IDs
//       if (sh.empty()) matched();
//       //for (const auto& p : sh)
//       //  Group::thisProxy[ p.first ].sharededges( CkMyPe(), p.second );
//       auto ish = sh.cbegin();
//       auto ishc = shc.cbegin();
//       while (ish != sh.cend()) {
//         Group::thisProxy[ ish->first ].
//          sharededges( CkMyPe(), ish->second, ishc->second );
//         ++ish;
//         ++ishc;
//       }
    }

  private:
    //! Host proxy
    HostProxy m_host;
    //! Worker proxy
    WorkerProxy m_worker;
    //! Linear system merger proxy
    LinSysMergerProxy m_linsysmerger;
    //! Particle writer proxy
    ParticleWriterProxy m_particlewriter;
    //! Number of fellow PEs to send elem IDs to
    std::size_t m_npe;
    //! Queue of requested node IDs from PEs
    std::vector< std::pair< int, std::unordered_set< std::size_t > > > m_req;
    //! Queue of requested edge-node IDs from PEs
    std::vector< std::pair< int, tk::UnsMesh::Edges > > m_reqed;
    //! Starting global mesh node ID for node reordering
    std::size_t m_start;
    //! \brief Counter for number of offsets
    //! \details This counts the to-be-received node IDs received while
    //!   computing global mesh node ID offsets for each PE rquired for node
    //!   reordering later
    std::size_t m_noffset;
    //! \brief Counter for number of masks of to-be-received global mesh node
    //!   IDs received
    //! \details This counts the to-be-received node ID masks received while
    //!   gathering the node IDs that need to be received (instead of uniquely
    //!   assigned) by each PE
    std::size_t m_nquery;
    //! Counter for number of maps with shared edges to be received
    std::size_t m_nshed;
    //! Tetrtahedron element coordinates of our chunk of the mesh
    std::array< std::vector< tk::real >, 3 > m_coord;
    //! Tetrtahedron element connectivity of our chunk of the mesh
    std::vector< std::size_t > m_tetinpoel;
    //! Global element IDs we read (our chunk of the mesh)
    std::vector< long > m_gelemid;
    //! Element centroid coordinates of our chunk of the mesh
    std::array< std::vector< tk::real >, 3 > m_centroid;
    //! Total number of chares across all PEs
    int m_nchare;
    //! Lower bound of node IDs our PE operates on after reordering
    std::size_t m_lower;
    //! Upper bound of node IDs our PE operates on after reordering
    std::size_t m_upper;
    //! \brief Global mesh node ids (element connectivity) associated to chares
    //!   owned
    //! \details Before reordering this map stores (old) global mesh node IDs
    //!   corresponding to the ordering as in the mesh file. After reordering it
    //!   stores the (new) global node IDs the chares contribute to.
    std::unordered_map< int, std::vector< std::size_t > > m_node;
    //! \brief Temporary communication map used to receive global mesh node IDs
    //! \details This map, on each PE, associates the list of global mesh point
    //!   indices to fellow PE IDs from which we will receive new node IDs
    //!   during reordering.
    std::map< int, std::unordered_set< std::size_t > > m_ncomm;
    //! \brief Temporary communication map used to receive global mesh edges
    //! \details This map, on each PE, associates the list of global mesh edges
    //!   indices to fellow PE IDs from which we will receive new nodes IDs
    //!   associated to edges during reordering.
    std::unordered_map< int, tk::UnsMesh::Edges > m_ecomm;
    //! \brief Communication map used for distributed mesh node reordering
    //! \details This map, on each PE, associates the list of global mesh point
    //!   indices to fellow PE IDs from which we will receive new node IDs
    //!   during reordering. Only data that will be received from PEs with a
    //!   lower index are stored.
    std::unordered_map< int, std::unordered_set<std::size_t> > m_ncommunication;
    //! \brief Communication map used for distributed mesh edge-node reordering
    //! \details This map, on each PE, associates the list of global mesh edges
    //!   to fellow PE IDs from which we will receive new node IDs associated to
    //!   edges during reordering. Only data that will be received from PEs with
    //!   a lower index are stored.
    std::unordered_map< int, tk::UnsMesh::Edges > m_ecommunication;
    //! \brief Unique global node IDs chares on our PE will contribute to in a
    //!   linear system
    std::set< std::size_t > m_nd;
    //! \brief Unique global edges whose nodes chares on our PE will contribute
    //!   to in a linear system
    tk::UnsMesh::Edges m_ed;
    //! \brief Map associating new node IDs (as in producing contiguous-row-id
    //!   linear system contributions) to old node IDs (as in file)
    std::unordered_map< std::size_t, std::size_t > m_newnd;
    //! \brief Map associating new node IDs to edges
    tk::UnsMesh::EdgeNodes m_newed;
    //! \brief Maps associating old node IDs to new node IDs categorized by
    //!   chares.
    //! \details Maps associating old node IDs (as in file) to new node IDs (as
    //!   in producing contiguous-row-id linear system contributions) associated
    //!   to chare IDs (outer key). This is basically the inverse of m_newnd and
    //!   categorized by chares. Note that this does not contain the nodes newly
    //!   added during initial uniform mesh refinement.
    //! \note Used for looking up boundary conditions, see, e.g., Carrier::bc()
    std::unordered_map< int,
      std::unordered_map< std::size_t, std::size_t > > m_chnodemap;
    //! \brief Maps associating new node IDs to edges (a pair of old node IDs)
    //!   in tk::UnsMesh::EdgeNodes maps, associated to and categorized by
    //!   chares.
    //! \details Maps associating new node IDs (as in producing
    //!   contiguous-row-id linear system contributions) to edges (a pair of old
    //!   node IDs, as in file) associated to chare IDs (outer key) for only
    //!   the nodes newly added as a result of initial uniform refinement.
    //! \note Used for looking up boundary conditions, see, e.g., Carrier::bc()
    std::unordered_map< int, tk::UnsMesh::EdgeNodes > m_chedgenodes;
    //! Communication cost of linear system merging for our PE
    tk::real m_cost;
    //! \brief Map associating a set of chare IDs to old global mesh node IDs
    //! \details Note that a single global mesh ID can be associated to multiple
    //!  chare IDs as multiple chares can contribute to a single mesh node.
    std::unordered_map< std::size_t, std::vector< int > > m_cn;
    //! \brief Global mesh node IDs associated to chare IDs bordering the mesh
    //!   chunk held by and associated to chare IDs we own
    //! \details msum: mesh chunks surrounding mesh chunks and their neighbor
    //!   points. Outer key: chare IDs we own whose neighbors are stored, inner
    //!   key: chare IDs of those chares that hold mesh chunks surrounding the
    //!   outer-key chare's mesh, values: global reordered mesh node indices
    //!   along the border of chares (at which the chares will need to
    //!   communicate).
    std::unordered_map< int,
      std::unordered_map< int, std::unordered_set< std::size_t > > > m_msum;
    //! Lower-PE-assigned associated to own-generated edge-node IDs
    //! \details Used for communicating and matching edge-nodes generated during
    //!   initial uniform mesh refinement
    std::unordered_map< std::size_t, std::size_t > m_sharedEdgeNodes;

    tk::UnsMesh::EdgeNodes m_edgenodes;
    decltype(m_node) m_newconn;

    //! Read our contiguously-numbered chunk of the mesh graph from file
    //! \param[in] er ExodusII mesh reader
    void readGraph( tk::ExodusIIMeshReader& er ) {
      // Get number of mesh points and number of tetrahedron elements in file
      er.readElemBlockIDs();
      auto nel = er.nelem( tk::ExoElemType::TET );
      // Read our contiguously-numbered chunk of tetrahedron element
      // connectivity from file and also generate and store the list of global
      // element indices for our chunk of the mesh
      auto npes = static_cast< std::size_t >( CkNumPes() );
      auto mype = static_cast< std::size_t >( CkMyPe() );
      auto chunk = nel / npes;
      auto from = mype * chunk;
      auto till = from + chunk;
      if (mype == npes-1) till += nel % npes;
      er.readElements( {{from, till-1}}, tk::ExoElemType::TET, m_tetinpoel );
      m_gelemid.resize( till-from );
      std::iota( begin(m_gelemid), end(m_gelemid), from );
      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.pegraph();
      signal2host_graph_complete( m_host, m_gelemid.size() );
    }

    // Compute element centroid coordinates
    //! \param[in] er ExodusII mesh reader
    void computeCentroids( tk::ExodusIIMeshReader& er ) {
      // Construct unique global mesh point indices of our chunk
      auto gid = m_tetinpoel;
      tk::unique( gid );
      // Read node coordinates of our chunk of the mesh elements from file
      auto ext = tk::extents( gid );
      m_coord = er.readNodes( ext );
      const auto& x = std::get< 0 >( m_coord );
      const auto& y = std::get< 1 >( m_coord );
      const auto& z = std::get< 2 >( m_coord );
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
        auto A = m_tetinpoel[e*4+0] - ext[0];
        auto B = m_tetinpoel[e*4+1] - ext[0];
        auto C = m_tetinpoel[e*4+2] - ext[0];
        auto D = m_tetinpoel[e*4+3] - ext[0];
        cx[e] = (x[A] + x[B] + x[C] + x[D]) / 4.0;
        cy[e] = (y[A] + y[B] + y[C] + y[D]) / 4.0;
        cz[e] = (z[A] + z[B] + z[C] + z[D]) / 4.0;
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
    void distribute( std::unordered_map< int, std::vector< std::size_t > >&& n )
    {
      auto dist = chareDistribution();
      for (int c=0; c<dist[1]; ++c) {
        auto chid = CkMyPe() * dist[0] + c;   // compute owned chare ID
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
      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.pedistributed();
      if (m_npe == 0) signal2host_distributed( m_host );
    }

    //! Compute chare distribution
    //! \return Chunksize, i.e., number of chares per all PEs except the last
    //!   one, and the number of chares for my PE
    //! \details Chare ids are distributed to PEs in a linear continguous order
    //!   with the last PE taking the remainder if the number of PEs is not
    //!   divisible by the number chares. For example, if nchare=7 and npe=3,
    //!   the chare distribution is PE0: 0 1, PE1: 2 3, and PE2: 4 5 6. As a
    //!   result of this distribution, all PEs will have their m_node map filled
    //!   with the global mesh node IDs associated to the Charm++ chare IDs each
    //!   PE owns.
    std::array< int, 2 > chareDistribution() const {
      auto chunksize = m_nchare / CkNumPes();
      auto mynchare = chunksize;
      if (CkMyPe() == CkNumPes()-1) mynchare += m_nchare % CkNumPes();
      return {{ chunksize, mynchare }};
    }

    //! Reorder global mesh node IDs
    void reorder() {
      // Activate SDAG waits for having requests arrive from other PEs for some
      // of our node IDs; and for computing/receiving lower and upper bounds of
      // global node IDs our PE operates on after reordering
      wait4prep();
      wait4bounds();
      wait4reorder();
      // In serial signal to the runtime system that we have participated in
      // reordering. This is required here because this is only triggered if
      // communication is required during mesh node reordering. See also
      // particioner.ci.
      if (CkNumPes() == 1) participated_complete();
      // Send out request for new global node IDs for nodes we do not reorder
      for (const auto& c : m_ncommunication)
        Group::thisProxy[ c.first ].request( CkMyPe(), c.second );
      // Send out request for new global node IDs for edges we do not reorder
      for (const auto& e : m_ecommunication)
        Group::thisProxy[ e.first ].request( CkMyPe(), e.second );
      // Lambda to decide if node ID is being assigned a new ID by us
      auto ownnode = [ this ]( std::size_t p ) {
        using Set = typename std::remove_reference<
                      decltype(m_ncommunication) >::type::value_type;
        return !std::any_of( m_ncommunication.cbegin(), m_ncommunication.cend(),
                             [&](const Set& s)
                             { return s.second.find(p) != s.second.cend(); } );
      };
      // Lambda to decide if edge-node ID is being assigned a new ID by us
      auto ownedge = [ this ]( const tk::UnsMesh::Edge& e ) {
        using Set = typename std::remove_reference<
                      decltype(m_ecommunication) >::type::value_type;
        return !std::any_of( m_ecommunication.cbegin(), m_ecommunication.cend(),
                             [&](const Set& s)
                             { return s.second.find(e) != s.second.cend(); } );
      };
      // Reorder our chunk of the mesh node IDs by looping through all of our
      // node IDs (resulting from reading our chunk of the mesh cells). We test
      // if we are to assign a new ID to a node ID, and if so, we assign new ID,
      // i.e., reorder, by constructing a map associating new to old IDs. We
      // also count up the reordered nodes, which also serves as the new node
      // id.
      for (auto p : m_nd) if (ownnode(p)) m_newnd[ p ] = m_start++;
      // Reorder our chunk of the mesh edges by looping through all of our edges
      // (resulting from initial uniform refinement of our chunk of the mesh
      // cells). We test if we are to assign a new ID to an edge, and if so, we
      // assign new ID, i.e., reorder, by constructing a map associating new
      // node IDs to edges. We also count up the reordered edge-nodes, which
      // also serves as the new node id.
      for (const auto& e : m_ed) if (ownedge(e)) m_newed[ e ] = m_start++;
      // Trigger SDAG wait indicating that reordering own node IDs are complete
      reorderowned_complete();
      // If all our nodes have new IDs assigned, signal that to the runtime
      if (m_newnd.size() == m_nd.size()) nodesreorder_complete();
      // If all our edges have new IDs assigned, signal that to the runtime
      if (m_newed.size() == m_ed.size()) edgesreorder_complete();
    }

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
      // Signal to the runtime system that we have participated in reordering
      participated_complete();
      // Find and return new node IDs to sender
      for (const auto& r : m_req) {
        std::unordered_map< std::size_t, std::size_t > n;
        for (auto p : r.second) n[ p ] = tk::cref_find( m_newnd, p );
        Group::thisProxy[ r.first ].neworder( n );
        tk::destroy( n );
      }
      tk::destroy(m_req); // Clear queue of requests just fulfilled
      // Find and return new node IDs associated to edges to sender
      for (const auto& r : m_reqed) {
        tk::UnsMesh::EdgeNodes n;
        for (const auto& e : r.second) n[ e ] = tk::cref_find( m_newed, e );
        Group::thisProxy[ r.first ].neworder( n );
        tk::destroy( n );
      }
      tk::destroy(m_reqed); // Clear queue of requests just fulfilled
      wait4prep();        // Re-enable SDAG wait for preparing new node requests
      // Re-enable trigger signaling that reordering of owned node IDs are
      // complete right away
      reorderowned_complete();
    }

    void refine() {
      tk::destroy( m_tetinpoel );
      for (const auto& c : m_node)
        for (auto i : c.second)
          m_tetinpoel.push_back( i );
      // generate data structure storing unique nodes connected to nodes
      std::unordered_map< std::size_t, std::unordered_set< std::size_t > > star;
      auto esup = tk::genEsup( m_tetinpoel, 4 );
      auto minmax = std::minmax_element( begin(m_tetinpoel), end(m_tetinpoel) );
      auto nnode = *minmax.second + 1;
      for (std::size_t p=0; p<nnode; ++p)
        for (std::size_t i=esup.second[p]+1; i<=esup.second[p+1]; ++i ) {
          for (std::size_t n=0; n<4; ++n) {
            auto q = m_tetinpoel[ esup.first[i] * 4 + n ];
            if (p < q) star[p].insert( q );
            if (p > q) star[q].insert( p );
          }
        }

      // associate new nodes to all unique edges
// std::cout << CkMyPe() << " addnode: ";
      auto& x = m_coord[0];
      auto& y = m_coord[1];
      auto& z = m_coord[2];
      nnode = tk::ExodusIIMeshReader( g_inputdeck.get< tag::cmd, tag::io,
                                        tag::input >() ).readHeader();
      for (const auto& s : star)
        for (auto q : s.second) {
//std::cout << '\n' << CkMyPe() << " add: " << s.first << '-' << q << "\t:" << nnode;
// std::array< std::size_t, 2 > p{{ s.first, q }};
// std::cout << s.first << '-' << q << ", coords: (" << m_coord[0][p[0]] << ',' << m_coord[1][p[0]] << ',' << m_coord[2][p[0]] << ")-(" << m_coord[0][p[1]] << ',' << m_coord[1][p[1]] << ',' << m_coord[2][p[1]] << ") ";
          x.push_back( (x[s.first]+x[q])/2.0 );
          y.push_back( (y[s.first]+y[q])/2.0 );
          z.push_back( (z[s.first]+z[q])/2.0 );
          m_edgenodes[ {{ s.first, q }} ] = nnode++;
}
// std::cout << '\n';
      tk::UnsMesh::Tet18 tet18;
      // assigne 8 new elements in place of each old element (1:8)
//std::cout << CkMyPe() << " inpoel: ";
      for (std::size_t e=0; e<m_tetinpoel.size()/4; ++e) {
        const auto A = m_tetinpoel[e*4+0];
        const auto B = m_tetinpoel[e*4+1];
        const auto C = m_tetinpoel[e*4+2];
        const auto D = m_tetinpoel[e*4+3];
//std::cout << A << ',' << B << ',' << C << ',' << D << ' ';
        const auto AB = tk::cref_find( m_edgenodes, {{ A,B }} );
        const auto AC = tk::cref_find( m_edgenodes, {{ A,C }} );
        const auto AD = tk::cref_find( m_edgenodes, {{ A,D }} );
        const auto BC = tk::cref_find( m_edgenodes, {{ B,C }} );
        const auto BD = tk::cref_find( m_edgenodes, {{ B,D }} );
        const auto CD = tk::cref_find( m_edgenodes, {{ C,D }} );
        // construct 8 new tets
        tk::UnsMesh::Child18 n{{  A, AB, AC, AD,
                                  B, BC, AB, BD,
                                  C, AC, BC, CD,
                                  D, AD, CD, BD,
                                 BC, CD, AC, BD,
                                 AB, BD, AC, AD,
                                 AB, BC, AC, BD,
                                 AC, BD, CD, AD }};
        tet18[ {{ A,B,C,D }} ] = n; // associate new elements to old one
      }
//std::cout << '\n';
      // update connectivity in global mesh node ids associated to chares owned
      for (const auto& conn : m_node) {
        //auto& ch = m_newconn[ conn.first ];
        auto& en = m_chedgenodes[ conn.first ];
        for (std::size_t e=0; e<conn.second.size()/4; ++e) {
          // find the 8 new elements replacing e
          const auto& n = tk::cref_find( tet18,
                                         {{ m_tetinpoel[e*4+0],
                                            m_tetinpoel[e*4+1],
                                            m_tetinpoel[e*4+2],
                                            m_tetinpoel[e*4+3] }} );
          // augment element connectivity (categorized by chares) with new cells
          //ch.insert( end(ch), begin(n), end(n) );
          const auto A = n[0];
          const auto B = n[4];
          const auto C = n[8];
          const auto D = n[12];
          const auto AB = n[1];
          const auto AC = n[2];
          const auto AD = n[3];
          const auto BC = n[5];
          const auto BD = n[7];
          const auto CD = n[11];
          en[ {{A,B}} ] = AB;
          en[ {{A,C}} ] = AC;
          en[ {{A,D}} ] = AD;
          en[ {{B,C}} ] = BC;
          en[ {{B,D}} ] = BD;
          en[ {{C,D}} ] = CD;
          // augment nodes associated to chares surrounding our mesh chunk
          for (auto& m : m_msum)
            for (auto& s : m.second) {
              bool a = false, b = false, c = false, d = false;
              if (s.second.find( A ) != end(s.second)) a = true;
              if (s.second.find( B ) != end(s.second)) b = true;
              if (s.second.find( C ) != end(s.second)) c = true;
              if (s.second.find( D ) != end(s.second)) d = true;
              // if an edge os on the chare boundary, its newly added nodes too
              if (a && b) s.second.insert( AB );
              if (a && c) s.second.insert( AC );
              if (a && d) s.second.insert( AD );
              if (b && c) s.second.insert( BC );
              if (b && d) s.second.insert( BD );
              if (c && d) s.second.insert( CD );
            }
        }
      }
    }

    //! Compute final result of reordering
    //! \details This member function is called when both those node IDs that we
    //!   assign a new ordering to as well as those assigned new IDs by other
    //!   PEs have been reordered (and we contribute to) and we are ready (on
    //!   this PE) to compute our final result of the reordering.
    void reordered() {
// tk::ExodusIIMeshWriter ow( "oldmesh." + std::to_string(CkMyPe()), tk::ExoWriter::CREATE );
// ow.writeMesh( tk::UnsMesh( m_tetinpoel, m_coord ) );

      // Construct maps associating old node IDs (as in file) to new node IDs
      // (as in producing contiguous-row-id linear system contributions)
      // associated to chare IDs (outer key). This is basically the inverse of
      // m_newnd and categorized by chares. Note that m_node at this point still
      // contains the old global node IDs the chares contribute to.
      for (const auto& c : m_node) {
        auto& old = m_chnodemap[ c.first ];
        for (auto p : c.second) {
          auto n = m_newnd.find(p);
          if (n != end(m_newnd)) old[ n->second ] = p;
        }
      }

      // Update edgenodes
      for (auto& c : m_chedgenodes)
        for (auto& e : c.second)
           e.second = tk::ref_find( m_newed, e.first );

//       // Update m_chedgenodes
//       for (auto& c : m_chedgenodes)
//         for (auto& p : c.second) {
//           auto n = m_newnd.find( p.second );
//           if (n != end(m_newnd)) p.second = n->second;
//         }

      // Augment connectivity with newly added edge-nodes
      decltype(m_node) newconn;
      for (const auto& c : m_node) {
        auto& n = newconn[ c.first ];
        auto& en = tk::cref_find( m_chedgenodes, c.first );
        for (std::size_t e=0; e<c.second.size()/4; ++e) {
          const auto A = c.second[e*4+0];
          const auto B = c.second[e*4+1];
          const auto C = c.second[e*4+2];
          const auto D = c.second[e*4+3];
          const auto AB = tk::cref_find( en, {{ A,B }} );
          const auto AC = tk::cref_find( en, {{ A,C }} );
          const auto AD = tk::cref_find( en, {{ A,D }} );
          const auto BC = tk::cref_find( en, {{ B,C }} );
          const auto BD = tk::cref_find( en, {{ B,D }} );
          const auto CD = tk::cref_find( en, {{ C,D }} );
          std::vector< std::size_t > newelems{{  A, AB, AC, AD,
                                                 B, BC, AB, BD,
                                                 C, AC, BC, CD,
                                                 D, AD, CD, BD,
                                                BC, CD, AC, BD,
                                                AB, BD, AC, AD,
                                                AB, BC, AC, BD,
                                                AC, BD, CD, AD }};
          n.insert( end(n), begin(newelems), end(newelems) );
        }
      }
      m_node = std::move( newconn );

      // Update our chare ID maps to now contain the new global node IDs
      // instead of the old ones
      for (auto& c : m_node)
        for (auto& p : c.second) {
          auto n = m_newnd.find(p);
          if (n != end(m_newnd)) p = n->second;
        }

//       tk::destroy( m_tetinpoel );
//       for (const auto& c : m_node)
//         for (auto i : c.second)
//           m_tetinpoel.push_back( i );

// std::cout << '\n' << CkMyPe() << " tetinpoel: ";
// for (auto c : m_tetinpoel) std::cout << c << ' ';
// std::cout << '\n';
// 
// std::cout << CkMyPe() << " node     : ";
// for (const auto& c : m_node) for (auto i : c.second) std::cout << i << ' ';
// std::cout << '\n';

// std::cout << '\n' << CkMyPe() << " newid, (o:n): ";
// for (const auto& c : m_newnd) std::cout << c.first << ':' << c.second << ' ';
// std::cout << '\n';

      // Update old global mesh node IDs to new ones (and add newly added ones)
      // associated to chare IDs bordering the mesh chunk held by and associated
      // to chare IDs we own
      for (auto& c : m_msum)
        for (auto& s : c.second) {
          decltype(s.second) n;
          for (auto p : s.second) {
            auto it = m_newnd.find(p);
            if (it != end(m_newnd)) n.insert( it->second );
          }
          s.second = std::move( n );
        }

      // Update unique global node IDs of chares our PE will contribute to to
      // now contain the new IDs resulting from reordering
      tk::destroy( m_nd );
      for (const auto& c : m_node) for (auto i : c.second) m_nd.insert( i );

//       auto nc = m_coord;
//       for (const auto& n : m_newnd) {
//         nc[0][ n.second ] = m_coord[0][ n.first ];
//         nc[1][ n.second ] = m_coord[1][ n.first ];
//         nc[2][ n.second ] = m_coord[2][ n.first ];
//       }
//       m_coord = std::move( nc );
// 
// tk::ExodusIIMeshWriter rw( "reordered_mesh." + std::to_string(CkMyPe()), tk::ExoWriter::CREATE );
// rw.writeMesh( tk::UnsMesh( m_tetinpoel, m_coord ) );

      // Uniformly refine our chunk of the mesh
      //refine();

// std::size_t j = 1;
// for (const auto& c : m_node) for (auto i : c.second) std::cout << '[' << CkMyPe() << 'b' << ']' << i << '(' << j++ << ')' << ' ';
// std::cout << '\n';

// tk::ExodusIIMeshWriter fw( "refined_mesh." + std::to_string(CkMyPe()), tk::ExoWriter::CREATE );
// fw.writeMesh( tk::UnsMesh( tk::cref_find(m_node,CkMyPe()), m_coord ) );

      signal2host_refined( m_host );
    }

    void matched() {

// std::size_t j = 0;
// for (const auto& c : m_node) for (auto i : c.second) std::cout << '[' << CkMyPe() << 'a' << ']' << i << '(' << j++ << ')' << ' ';
// std::cout << '\n';

// tk::ExodusIIMeshWriter fw( "matched_mesh." + std::to_string(CkMyPe()), tk::ExoWriter::CREATE );
// fw.writeMesh( tk::UnsMesh( tk::cref_find(m_node,CkMyPe()), m_coord ) );

      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.pereordered();
      // Compute lower and upper bounds of reordered node IDs our PE operates on
      bounds();
    }

    // Compute lower and upper bounds of reordered node IDs our PE operates on
    // \details This function computes the global row IDs at which the linear
    //   system will have a PE boundary. We simply find the largest node ID
    //   assigned on each PE by the reordering and use that as the upper global
    //   row index. Note that while this rarely results in equal number of rows
    //   assigned to PEs, potentially resulting in some load-imbalance, it
    //   yields a pretty good division reducing communication costs during the
    //   assembly of the linear system, which is more important than a slight
    //   (FLOP) load imbalance. Since the upper index for PE 1 is the same as
    //   the lower index for PE 2, etc., we find the upper indices and then the
    //   lower indices for all PEs are communicated.
    void bounds() {

// std::cout << '\n';
// std::cout << CkMyPe() << " chnodemap: ";
// for (const auto& c : m_chnodemap) {
//   std::cout << c.first << "> ";
//   for (const auto& n : c.second) std::cout << n.first << ' ';
// }
// std::cout << '\n';
// std::cout << CkMyPe() << " chedgenodes: ";
// for (const auto& c : m_chedgenodes) {
//   std::cout << c.first << "> ";
//   for (const auto& n : c.second) std::cout << n.second << ' ';
// }
// std::cout << '\n';

      m_upper = 0;
      using P1 = std::pair< const std::size_t, std::size_t >;
      for (const auto& c : m_chnodemap) {
        auto x = std::max_element( begin(c.second), end(c.second),
                 [](const P1& a, const P1& b){ return a.first < b.first; } );
        if (x->first > m_upper) m_upper = x->first;
      }
      using P2 = std::pair< const tk::UnsMesh::Edge, std::size_t >;
      for (const auto& c : m_chedgenodes) {
        auto x = std::max_element( begin(c.second), end(c.second),
                 [](const P2& a, const P2& b){ return a.second < b.second; } );
        if (x->second > m_upper) m_upper = x->second;
      }
//std::cout << CkMyPe() << ": " << m_upper << '\n';

      // The bounds are the dividers (global mesh point indices) at which the
      // linear system assembly is divided among PEs. However, Hypre and thus
      // LinSysMerger expect exclusive upper indices, so we increase the last
      // one by one here. Note that the cost calculation, Partitioner::cost()
      // also expects exclusive upper indices.
      if (CkMyPe() == CkNumPes()-1) ++m_upper;
      // Tell the runtime system that the upper bound has been computed
      upper_complete();
      // Set lower index for PE 0 as 0
      if (CkMyPe() == 0) lower(0);
      // All PEs except the last one send their upper indices as the lower index
      // for PE+1
      if (CkMyPe() < CkNumPes()-1)
        Group::thisProxy[ CkMyPe()+1 ].lower( m_upper );
    }

    //! \brief Create chare array elements on this PE and assign the global mesh
    //!   element IDs they will operate on
    //! \details We create chare array elements by calling the insert() member
    //!   function, which allows specifying the PE on which the array element is
    //!   created and we send each chare array element the global mesh element
    //!   connectivity, i.e., node IDs, it contributes to and the old->new node
    //!   ID map.
    void create() {
      // send progress report to host
      if ( g_inputdeck.get< tag::cmd, tag::feedback >() )
        m_host.pebounds();
      // Initiate asynchronous reduction across all Partitioner objects
      // computing the average communication cost of merging the linear system
      signal2host_avecost( m_host );
      // Create worker chare array elements
      createWorkers( chareDistribution() );
      // Broadcast our bounds of global node IDs to all linear system mergers
      m_linsysmerger.bounds( CkMyPe(), m_lower, m_upper );
    }

    //! Create chare array elements on this PE
    //! \param[in] dist Pair of 'chunksize', i.e., number of chares per all PEs
    //!   except the last one, and 'mynchare', i.e., the number of chares for my
    //!   PE. See also chareDistribution().
    void createWorkers( std::array< int, 2 >&& dist ) {
      for (int c=0; c<dist[1]; ++c) {
        // Compute chare ID
        auto cid = CkMyPe() * dist[0] + c;
        // Guard those searches that operate on empty containers in serial
        typename decltype(m_msum)::mapped_type msum;
        if (!m_msum.empty()) msum = tk::cref_find( m_msum, cid );
        typename decltype(m_chedgenodes)::mapped_type edno;
        if (!m_chedgenodes.empty()) edno = tk::cref_find( m_chedgenodes, cid );
        // Create worker array element
        m_worker[ cid ].insert( m_host,
                                m_linsysmerger,
                                m_particlewriter,
                                tk::cref_find( m_node, cid ),
                                msum,
                                tk::cref_find( m_chnodemap, cid ),
                                edno,
                                m_nchare,
                                CkMyPe() );
      }
      m_worker.doneInserting();
      // Free storage of global mesh node ids associated to chares owned as it
      // is no longer needed after creating the workers.
      tk::destroy( m_node );
      // Free maps associating old node IDs to new node IDs categorized by
      // chares as it is no longer needed after creating the workers.
      tk::destroy( m_chnodemap );
      // Free storage of map associating a set of chare IDs to old global mesh
      // node IDs as it is no longer needed after creating the workers.
      tk::destroy( m_cn );
      // Free storage of global mesh node IDs associated to chare IDs bordering
      // the mesh chunk held by and associated to chare IDs we own as it is no
      // longer needed after creating the workers.
      tk::destroy( m_msum );
    }

    //! Compute communication cost of linear system merging for our PE
    //! \param[in] l Lower global row ID of linear system this PE works on
    //! \param[in] u Upper global row ID of linear system this PE works on
    //! \return Communication cost of merging the linear system for our PE
    //! \details The cost is a real number between 0 and 1, defined as the
    //!   number of mesh points we do not own, i.e., need to send to some other
    //!   PE, divided by the total number of points we contribute to. The lower
    //!   the better.
    tk::real cost( std::size_t l, std::size_t u ) {
      std::size_t ownpts = 0, compts = 0;
      for (auto p : m_nd) if (p >= l && p < u) ++ownpts; else ++compts;
      // Free storage of unique global node IDs chares on our PE will contribute
      // to in a linear system as it is no longer needed after computing the
      // communication cost.
      tk::destroy( m_nd );
      return static_cast<tk::real>(compts) /
             static_cast<tk::real>(ownpts + compts);
    }

    //! \brief Signal back to host that we have done our part of reading the
    //!   mesh graph
    //! \details Signaling back is done via a Charm++ typed reduction, which
    //!   also computes the sum of the number of mesh cells our PE operates on.
    void signal2host_graph_complete( const CProxy_Transporter& host,
                                     uint64_t nelem ) {
      Group::contribute(sizeof(uint64_t), &nelem, CkReduction::sum_int,
                        CkCallback(CkReductionTarget(Transporter,load), host));
    }
    //! Compute average communication cost of merging the linear system
    //! \details This is done via a Charm++ typed reduction, adding up the cost
    //!   across all PEs and reducing the result to our host chare.
    void signal2host_avecost( const CProxy_Transporter& host ) {
      m_cost = cost( m_lower, m_upper );
      Group::contribute( sizeof(tk::real), &m_cost, CkReduction::sum_double,
                         CkCallback( CkReductionTarget(Transporter,aveCost),
                         host ));
    }
    //! \brief Compute standard deviation of the communication cost of merging
    //!   the linear system
    //! \param[in] var Square of the communication cost minus the average for
    //!   our PE.
    //! \details This is done via a Charm++ typed reduction, adding up the
    //!   squares of the communication cost minus the average across all PEs and
    //!   reducing the result to our host chare.
    void signal2host_stdcost( const CProxy_Transporter& host, tk::real var ) {
      Group::contribute( sizeof(tk::real), &var, CkReduction::sum_double,
                         CkCallback( CkReductionTarget(Transporter,stdCost),
                         host ));
    }
    //! Signal back to host that we are ready for partitioning the mesh
    void signal2host_setup_complete( const CProxy_Transporter& host ) {
      Group::contribute(
        CkCallback(CkIndex_Transporter::redn_wrapper_partition(NULL), host ));
    }
    //! \brief Signal host that we are done our part of distributing mesh node
    //!   IDs and we are ready for preparing (flattening) data for reordering
    void signal2host_distributed( const CProxy_Transporter& host ) {
      Group::contribute(
        CkCallback(CkIndex_Transporter::redn_wrapper_distributed(NULL), host ));
    }
    //! \brief Signal host that we are ready for computing the communication
    //!   map, required for parallel distributed global mesh node reordering
    void signal2host_flattened( const CProxy_Transporter& host ) {
      Group::contribute(
        CkCallback(CkIndex_Transporter::redn_wrapper_flattened(NULL), host ));
    }
    //! \brief Signal host that we are ready for communicating shared edge-node
    //!   IDs after initial uniform mesh refinement
    void signal2host_refined( const CProxy_Transporter& host ) {
      Group::contribute(
        CkCallback(CkIndex_Transporter::redn_wrapper_refined(NULL), host ));
    }
};

#if defined(__clang__)
  #pragma clang diagnostic pop
#endif

} // inciter::

#define CK_TEMPLATES_ONLY
#include "NoWarning/partitioner.def.h"
#undef CK_TEMPLATES_ONLY

#endif // Partitioner_h
