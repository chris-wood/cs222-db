#include "dbgout.h"

namespace dbg
{
    Dbgout out;

    Dbgout::Dbgout()
        : _out(std::cout),
          _logLevel(DEFAULT_VERBOSITY),
          _verbosity(DEFAULT_VERBOSITY)
    {
    }

    Dbgout::~Dbgout()
    {
    }
}
