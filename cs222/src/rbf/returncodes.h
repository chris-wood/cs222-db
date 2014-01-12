#ifndef _returncodes_h_
#define _returncodes_h_

namespace rc
{
    enum ReturnCodes
    {
        UNKNOWN_FAILURE = -1,
        OK = 0,

        FEATURE_NOT_YET_IMPLEMENTED,

        FILE_NOT_FOUND,
        FILE_ALREADY_EXISTS,
        FILE_CORRUPT,
        FILE_COULD_NOT_APPEND,

        FILE_HANDLE_ALREADY_INITIALIZED,
        FILE_HANDLE_NOT_INITIALIZED,
        FILE_HANDLE_UNKNOWN,

        RECORD_DOES_NOT_EXIST,
        RECORD_CORRUPT
    };
}

#endif // _returncodes_h_
