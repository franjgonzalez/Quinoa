//******************************************************************************
/*!
  \file      src/Random/MKLException.h
  \author    J. Bakosi
  \date      Sat 10 Nov 2012 07:01:55 PM MST
  \copyright Copyright 2005-2012, Jozsef Bakosi, All rights reserved.
  \brief     MKLException class declaration
  \details   MKLException class declaration
*/
//******************************************************************************
#ifndef MKLException_h
#define MKLException_h

#include <string>
#include <map>

using namespace std;

#include <RandomException.h>

namespace Quinoa {

//! MKL exception types
enum MKLExceptType { MKL_UNKNOWN_METHOD=0,
                     MKL_UNKNOWN_TABLE,
                     MKL_UNKNOWN_STREAM,
                     MKL_BAD_NTHREADS,
                     MKL_BAD_NUMBER,
                     MKL_VSL_ERROR,
                     NUM_MKL_EXCEPT
};

//! MKL exception error messages
const string MKLMsg[NUM_MKL_EXCEPT] = {
  "Unknown VSL generation method",
  "Random number table not found",
  "Random number stream not found",
  "Wrong number of threads",
  "Bad number of items"
  "VSL ",
};

//! MKLException : RandomException
class MKLException : public RandomException {

  public:
    //! Constructor
    MKLException(ExceptType except,
                 MKLExceptType mklExcept,
                 const string& file,
                 const string& func,
                 const unsigned int& line) :
      RandomException(except, RND_MKL, file, func, line), m_except(mklExcept) {}

    //! Move constructor, necessary for throws, default compiler generated
    MKLException(MKLException&&) = default;

    //! Don't permit copy constructor
    // ICC: should be deleted and private
    MKLException(const MKLException&);

    //! Destructor
    virtual ~MKLException() {}

    //! Handle MKLException
    virtual ErrCode handleException(Driver* driver);

  private:
    //! Don't permit copy assignment
    MKLException& operator=(const MKLException&) = delete;
    //! Don't permit move assignment
    MKLException& operator=(MKLException&&) = delete;

    //! MKL exception type (MKL_UNIMPLEMENTED, MKL_UNKNOWN_METHOD, etc.)
    MKLExceptType m_except;
};

} // namespace Quinoa

#endif // MKLException_h