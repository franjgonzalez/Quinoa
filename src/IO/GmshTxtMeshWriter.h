//******************************************************************************
/*!
  \file      src/IO/GmshTxtMeshWriter.h
  \author    J. Bakosi
  \date      Mon Oct  7 10:05:41 2013
  \copyright Copyright 2005-2012, Jozsef Bakosi, All rights reserved.
  \brief     GmshTxtMeshWriter class declaration
  \details   GmshTxtMeshWriter class declaration
*/
//******************************************************************************
#ifndef GmshTxtMeshWriter_h
#define GmshTxtMeshWriter_h

#include <string>

#include <Writer.h>
#include <GmshMesh.h>

namespace quinoa {

//! GmshTxtMeshWriter : Writer
class GmshTxtMeshWriter : public tk::Writer {

  public:
    //! Constructor
    explicit GmshTxtMeshWriter(const std::string& filename,
                               GmshMesh& mesh) :
      Writer(filename),
      m_mesh(mesh) {}

    //! Destructor, default compiler generated
    ~GmshTxtMeshWriter() noexcept override = default;

    //! Write Gmsh mesh to file
    void write();

  private:
    //! Don't permit copy constructor
    GmshTxtMeshWriter(const GmshTxtMeshWriter&) = delete;
    //! Don't permit copy assigment
    GmshTxtMeshWriter& operator=(const GmshTxtMeshWriter&) = delete;
    //! Don't permit move constructor
    GmshTxtMeshWriter(GmshTxtMeshWriter&&) = delete;
    //! Don't permit move assigment
    GmshTxtMeshWriter& operator=(GmshTxtMeshWriter&&) = delete;

    //! Write mandatory "$MeshFormat--$EndMeshFormat" section
    void writeMeshFormat();

    //! Write "$Nodes--$EndNodes" section
    void writeNodes();

    //! Write "$Elements--$EndElements" section
    void writeElements();

    //! Write "$PhysicalNames--$EndPhysicalNames" section
    void writePhysicalNames();

    GmshMesh& m_mesh;         //!< Mesh object
};

} // quinoa::

#endif // GmshTxtMeshWriter_h
