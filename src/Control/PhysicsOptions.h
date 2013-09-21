//******************************************************************************
/*!
  \file      src/Control/PhysicsOptions.h
  \author    J. Bakosi
  \date      Sat 21 Sep 2013 07:07:07 AM MDT
  \copyright Copyright 2005-2012, Jozsef Bakosi, All rights reserved.
  \brief     Physics options and associations
  \details   Physics options and associations
*/
//******************************************************************************
#ifndef PhysicsOptions_h
#define PhysicsOptions_h

#include <map>

#include <Exception.h>
#include <Toggle.h>
#include <QuinoaKeywords.h>

namespace quinoa {
namespace sel {

//! Physics types
enum class PhysicsType : uint8_t { NO_PHYSICS=0,
                                   HOMOGENEOUS_MIX,
                                   HOMOGENEOUS_HYDRO,
                                   HOMOGENEOUS_RAYLEIGH_TAYLOR,
                                   SPINSFLOW,
                                   RNGTEST };

//! Class with base templated on the above enum class with associations
class Physics : public Toggle<PhysicsType> {

  public:
    //! Constructor: pass associations references to base, which will handle
    //! class-user interactions
    explicit Physics() : Toggle<PhysicsType>(names, values) {}

  private:
    //! Don't permit copy constructor
    Physics(const Physics&) = delete;
    //! Don't permit copy assigment
    Physics& operator=(const Physics&) = delete;
    //! Don't permit move constructor
    Physics(Physics&&) = delete;
    //! Don't permit move assigment
    Physics& operator=(Physics&&) = delete;

    //! Get access to physics keywords
    const grm::kw::hommix hommix {};
    const grm::kw::homhydro homhydro {};
    const grm::kw::homrt homrt {};
    const grm::kw::spinsflow spinsflow {};
    const grm::kw::rngtest rngtest {};

    //! Enums -> names
    const std::map<PhysicsType, std::string> names {
      { PhysicsType::NO_PHYSICS, "No physics" },
      { PhysicsType::HOMOGENEOUS_MIX, hommix.name() },
      { PhysicsType::HOMOGENEOUS_HYDRO, "Homogeneous hydrodynamics" },
      { PhysicsType::HOMOGENEOUS_RAYLEIGH_TAYLOR,
        "Homogeneous Rayleigh-Taylor" },
      { PhysicsType::SPINSFLOW,
        "Standalone-Particle Incompressible Navier-Stokes Flow" },
      { PhysicsType::RNGTEST, "Random number generator tests" }
    };

    //! keywords -> Enums
    const std::map<std::string, PhysicsType> values {
      { "no_physics", PhysicsType::NO_PHYSICS },
      { hommix.string(), PhysicsType::HOMOGENEOUS_MIX },
      { homhydro.string(), PhysicsType::HOMOGENEOUS_HYDRO },
      { homrt.string(), PhysicsType::HOMOGENEOUS_RAYLEIGH_TAYLOR },
      { spinsflow.string(), PhysicsType::SPINSFLOW },
      { rngtest.string(), PhysicsType::RNGTEST }
    };
};

} // sel::
} // quinoa::

#endif // PhysicsOptions_h
