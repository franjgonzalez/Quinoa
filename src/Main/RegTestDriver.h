//******************************************************************************
/*!
  \file      src/Main/RegTestDriver.h
  \author    J. Bakosi
  \date      Sun 22 Mar 2015 09:12:40 PM MDT
  \copyright 2012-2015, Jozsef Bakosi.
  \brief     Regression test harness driver
  \details   Regression test harness driver.
*/
//******************************************************************************
#ifndef RegTestDriver_h
#define RegTestDriver_h

#include <RegTestPrint.h>
#include <RegTest/CmdLine/CmdLine.h>

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wconversion"
#endif

#include <regtest.decl.h>

#if defined(__clang__) || defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

extern CProxy_Main mainProxy;

namespace regtest {

//! Regression test suite driver used polymorphically with tk::Driver
class RegTestDriver {

  public:
    //! Constructor
    explicit RegTestDriver( const RegTestPrint& print,
                            const ctr::CmdLine& cmdline );

    //! Execute driver
    void execute() const;

  private:
    const RegTestPrint& m_print;        //!< Pretty printer
    const ctr::CmdLine& m_cmdline;      //!< User input on command-line
};

} // regtest::

#endif // RegTestDriver_h
