#include "dbgout.h"

namespace dbg
{
    Dbgout out;

    Dbgout::Dbgout()
        : _out(std::cerr),
          _logLevel(DEFAULT_VERBOSITY),
          _verbosity(DEFAULT_VERBOSITY)
    {
    }

    Dbgout::~Dbgout()
    {
    }
}
