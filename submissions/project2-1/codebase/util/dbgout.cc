#include "dbgout.h"

namespace dbg
{
    Dbgout out;

    Dbgout::Dbgout()
#if DEBUG_ENABLED
        : _out(std::cerr),
          _logLevel(DEFAULT_VERBOSITY),
          _verbosity(DEFAULT_VERBOSITY)
#endif
    {
#if REDIRECT_COUT_TO_FILE
        // This is basically only for QTCreator because it can't handle debugging a program at the same time it's redirecting output
        std::ofstream out("out.txt");
        _savedCout = std::cout.rdbuf(); //save old buf
        std::cout.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
#endif
    }

    Dbgout::~Dbgout()
    {
    }
}
