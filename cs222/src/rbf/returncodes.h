#ifndef _returncodes_h_
#define _returncodes_h_

namespace rc
{
    enum ReturnCode
    {
        UNKNOWN_FAILURE = -1,
        OK = 0,

        FEATURE_NOT_YET_IMPLEMENTED,

        FILE_NOT_FOUND,
        FILE_ALREADY_EXISTS,
        FILE_CORRUPT,
        FILE_COULD_NOT_OPEN,
        FILE_COULD_NOT_DELETE,

        FILE_HANDLE_ALREADY_INITIALIZED,
        FILE_HANDLE_NOT_INITIALIZED,
        FILE_HANDLE_UNKNOWN,

        RECORD_DOES_NOT_EXIST,
        RECORD_CORRUPT
    };
/*
    const char* toString(ReturnCode rc)
    {
        switch(rc)
        {
            UNKNOWN_FAILURE:                    return "UNKNOWN_FAILURE";
            OK:                                 return "OK";
            FEATURE_NOT_YET_IMPLEMENTED:        return "FEATURE_NOT_YET_IMPLEMENTED";
            FILE_NOT_FOUND:                     return "FILE_NOT_FOUND";
            FILE_ALREADY_EXISTS:                return "FILE_ALREADY_EXISTS";
            FILE_CORRUPT:                       return "FILE_CORRUPT";
            FILE_COULD_NOT_APPEND:              return "FILE_COULD_NOT_APPEND";
            FILE_HANDLE_ALREADY_INITIALIZED:    return "FILE_HANDLE_ALREADY_INITIALIZED";
            FILE_HANDLE_NOT_INITIALIZED:        return "FILE_HANDLE_NOT_INITIALIZED";
            FILE_HANDLE_UNKNOWN:                return "FILE_HANDLE_UNKNOWN";
            RECORD_DOES_NOT_EXIST:              return "RECORD_DOES_NOT_EXIST";
            RECORD_CORRUPT:                     return "RECORD_CORRUPT";
        }

        return "UNKNOWN_ERROR_CODE";
    }
*/
}

#endif // _returncodes_h_
