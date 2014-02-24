#include "returncodes.h"

bool ASSERT_ON_BAD_RETURN = GLOBAL_ASSERT_ON_BAD_RETURN;

namespace rc
{
    const char* rcToString(int rc)
    {
        switch((ReturnCode)rc)
        {
        case END_OF_FILE:                           return "END_OF_FILE";
        case UNKNOWN_FAILURE:                       return "UNKNOWN_FAILURE";
        case OK:                                    return "OK";
        case FEATURE_NOT_YET_IMPLEMENTED:           return "FEATURE_NOT_YET_IMPLEMENTED";
        case FILE_PAGE_NOT_FOUND:                   return "FILE_PAGE_NOT_FOUND";
        case FILE_SEEK_FAILED:                      return "FILE_SEEK_FAILED";
        case FILE_NOT_FOUND:                        return "FILE_NOT_FOUND";
        case FILE_ALREADY_EXISTS:                   return "FILE_ALREADY_EXISTS";
        case FILE_CORRUPT:                          return "FILE_CORRUPT";
        case FILE_COULD_NOT_OPEN:                   return "FILE_COULD_NOT_APPEND";
        case FILE_COULD_NOT_DELETE:                 return "FILE_COULD_NOT_DELETE";
        case FILE_HANDLE_ALREADY_INITIALIZED:       return "FILE_HANDLE_ALREADY_INITIALIZED";
        case FILE_HANDLE_NOT_INITIALIZED:           return "FILE_HANDLE_NOT_INITIALIZED";
        case FILE_HANDLE_UNKNOWN:                   return "FILE_HANDLE_UNKNOWN";
        case FILE_NOT_OPENED:                       return "FILE_NOT_OPENED";
        case RECORD_DOES_NOT_EXIST:                 return "RECORD_DOES_NOT_EXIST";
        case RECORD_CORRUPT:                        return "RECORD_CORRUPT";
        case RECORD_EXCEEDS_PAGE_SIZE:              return "RECORD_EXCEEDS_PAGE_SIZE";
		case RECORD_DELETED:						return "RECORD_DELETED";
        case RECORD_IS_ANCHOR:                      return "RECORD_IS_ANCHOR";
        case HEADER_SIZE_CORRUPT:                   return "HEADER_SIZE_CORRUPT";
        case HEADER_PAGESIZE_MISMATCH:              return "HEADER_PAGESIZE_MISMATCH";
        case HEADER_VERSION_MISMATCH:               return "HEADER_VERSION_MISMATCH";
        case HEADER_FREESPACE_LISTS_MISMATCH:       return "HEADER_FREESPACE_LISTS_MISMATCH";
        case HEADER_FREESPACE_LISTS_CORRUPT:        return "HEADER_FREESPACE_LIST_CORRUPT";
        case HEADER_SIZE_TOO_LARGE:                 return "HEADER_SIZE_TOO_LARGE";
        case PAGE_CANNOT_BE_ORGANIZED:              return "PAGE_CANNOT_BE_ORGANIZED";
		case PAGE_NUM_INVALID:						return "PAGE_NUM_INVALID";
        case RECORD_SIZE_INVALID:                   return "RECORD_SIZE_INVALID";
		case TABLE_NOT_FOUND:						return "TABLE_NOT_FOUND";
		case TABLE_ALREADY_CREATED:					return "TABLE_ALREADY_CREATED";
		case TABLE_NAME_TOO_LONG:					return "TABLE_NAME_TOO_LONG";
        case ATTRIBUTE_INVALID_TYPE:                return "ATTRIBUTE_INVALID_TYPE";
		case ATTRIBUTE_NOT_FOUND:					return "ATTRIBUTE_NOT_FOUND";
		case ATTRIBUTE_NAME_TOO_LONG:				return "ATTRIBUTE_NAME_TOO_LONG";
		case ATTRIBUTE_COUNT_MISMATCH:				return "ATTRIBUTE_COUNT_MISMATCH";
		case ATTRIBUTE_LENGTH_INVALID:				return "ATTRIBUTE_LENGTH_INVALID";
		case BTREE_INDEX_PAGE_INITIALIZATION_FAILED:return "BTREE_INDEX_PAGE_INITIALIZATION_FAILED";
        case BTREE_INDEX_LEAF_ENTRY_NOT_FOUND:      return "BTREE_INDEX_LEAF_ENTRY_NOT_FOUND";
        case BTREE_INDEX_PAGE_FULL:                 return "BTREE_INDEX_PAGE_FULL";
        case BTREE_CANNOT_MERGE_PAGES_TOO_FULL:     return "BTREE_CANNOT_MERGE_PAGES_TOO_FULL";
        case BTREE_CANNOT_MERGE_LEAF_AND_NONLEAF:   return "BTREE_CANNOT_MERGE_LEAF_AND_NONLEAF";
		case BTREE_CANNOT_FIND_LEAF:				return "BTREE_CANNOT_FIND_LEAF";
		case BTREE_KEY_TOO_LARGE:					return "BTREE_KEY_TOO_LARGE";

		case OUT_OF_MEMORY:							return "OUT_OF_MEMORY";
        }

        return "UNKNOWN_ERROR_CODE";
    }
}
