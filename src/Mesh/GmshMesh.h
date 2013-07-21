//******************************************************************************
/*!
  \file      src/Mesh/GmshMesh.h
  \author    J. Bakosi
  \date      Fri 12 Jul 2013 10:25:57 PM MDT
  \copyright Copyright 2005-2012, Jozsef Bakosi, All rights reserved.
  \brief     Gmsh mesh class declaration
  \details   Gmsh mesh class declaration
*/
//******************************************************************************
#ifndef GmshMesh_h
#define GmshMesh_h

#include <vector>

#include <Memory.h>

namespace Quinoa {

//! Base names for various mesh memory entries
const std::string     NODES_NAME = "nodes";
const std::string    COORDS_NAME = "coords";
const std::string     LINES_NAME = "lines";
const std::string TRIANGLES_NAME = "triangles";

//! Gmsh mesh class
class GmshMesh {

  public:
    //! Constructor: zero memory entry pointers held
    explicit GmshMesh(Memory* const memory) noexcept;

    //! Destructor: free memory entries held
    virtual ~GmshMesh() noexcept;

    //! Set mesh version
    void setVersion(const real version) { m_version = version; }

    //! Set mesh type
    void setType(const int type) { m_type = type; }

    //! Set mesh data size
    void setDatasize(const int datasize) { m_datasize = datasize; }

    //! Get mesh version
    real getVersion() const { return m_version; }

    //! Get mesh type
    int getType() const { return m_type; }

    //! Get mesh data size
    int getDatasize() const { return m_datasize; }

    //! Allocate memory for mesh
    void alloc(const int nnodes, const int nlins, const int ntris);

    //! Reserve capacity to store element connectivities and tags
    void reserveElem(const int nlines, const int ntriangles) noexcept;

    //! Add a line element
    void addLine(const std::vector<int>& nodes) { m_linpoel.push_back(nodes); }

    //! Add a triangle element
    void addTriangle(const std::vector<int>& nodes) {
      m_tinpoel.push_back(nodes);
    }

    //! Add line element tags
    void addLineTags(const std::vector<int>& tags) { m_lintag.push_back(tags); }

    //! Add triangle element tags
    void addTriangleTags(const std::vector<int>& tags) {
      m_tritag.push_back(tags);
    }

    //! Coords accessor
    real* getCoord() const { return m_coord.ptr; }

    //! NodeId accessor
    int* getNodeId() const { return m_nodeId.ptr; }

    //! Line element id accessor
    int* getLineId() const { return m_lineId.ptr; }

    //! Triangle element id accessor
    int* getTriangleId() const { return m_triangleId.ptr; }

    //! Number of nodes accessor
    int getNnodes() const { return m_memory->getNumber(m_nodeId.id); }

    //! Line elements connectivity accessor
    const std::vector<std::vector<int>> getLinpoel() const { return m_linpoel; }

    //! Line element tags accessor
    const std::vector<std::vector<int>> getLintag() const { return m_lintag; }

    //! Triangles elements connectivity accessor
    const std::vector<std::vector<int>> getTinpoel() const { return m_tinpoel; }

    //! Triangle element tags accessor
    const std::vector<std::vector<int>> getTritag() const { return m_tritag; }

    //! Echo element tags and connectivity in all element sets
    void echoElemSets() const;

  private:
    //! Don't permit copy constructor
    GmshMesh(const GmshMesh&) = delete;
    //! Don't permit assigment constructor
    GmshMesh& operator=(const GmshMesh&) = delete;
    //! Don't permit move constructor
    GmshMesh(GmshMesh&&) = delete;
    //! Don't permit move assignment
    GmshMesh& operator=(GmshMesh&&) = delete;

    Memory* const m_memory;                  //!< Memory object pointer

    real m_version;                          //!< Mesh version in mesh file
    int m_type;                              //!< File type in mesh file
    int m_datasize;                          //!< Data size in mesh file

    Data<real> m_coord;                      //!< Node coordinates
    Data<int> m_nodeId;                      //!< Node Ids
    Data<int> m_lineId;                      //!< Line element Ids
    Data<int> m_triangleId;                  //!< Triangle element Ids

    std::vector<std::vector<int>> m_linpoel; //!< Line elements connectivity
    std::vector<std::vector<int>> m_tinpoel; //!< Triangle elements connectivity
    std::vector<std::vector<int>> m_lintag;  //!< Line element tags
    std::vector<std::vector<int>> m_tritag;  //!< Triangle element tags
};

} // namespace Quinoa

#endif // GmshMesh_h