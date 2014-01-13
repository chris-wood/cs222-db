#include "returncodes.h"

namespace rc
{
    const char* rcToString(int rc)
    {
        switch((ReturnCode)rc)
        {
            case UNKNOWN_FAILURE:                    return "UNKNOWN_FAILURE";
            case OK:                                 return "OK";

            case FEATURE_NOT_YET_IMPLEMENTED:        return "FEATURE_NOT_YET_IMPLEMENTED";

            case FILE_NOT_FOUND:                     return "FILE_NOT_FOUND";
            case FILE_ALREADY_EXISTS:                return "FILE_ALREADY_EXISTS";
            case FILE_CORRUPT:                       return "FILE_CORRUPT";
            case FILE_COULD_NOT_OPEN:                return "FILE_COULD_NOT_APPEND";
            case FILE_COULD_NOT_DELETE:              return "FILE_COULD_NOT_DELETE";

            case FILE_HANDLE_ALREADY_INITIALIZED:    return "FILE_HANDLE_ALREADY_INITIALIZED";
            case FILE_HANDLE_NOT_INITIALIZED:        return "FILE_HANDLE_NOT_INITIALIZED";
            case FILE_HANDLE_UNKNOWN:                return "FILE_HANDLE_UNKNOWN";

            case RECORD_DOES_NOT_EXIST:              return "RECORD_DOES_NOT_EXIST";
            case RECORD_CORRUPT:                     return "RECORD_CORRUPT";
        }

        return "UNKNOWN_ERROR_CODE";
    }
}
