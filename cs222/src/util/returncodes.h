#ifndef _returncodes_h_
#define _returncodes_h_

//#define ASSERT_ON_BAD_RETURN

#ifdef ASSERT_ON_BAD_RETURN
	#define RETURN_ON_ERR(ret) {if((ret) != rc::OK) {assert(false); return ret;}}
#else
	#define RETURN_ON_ERR(ret) {if((ret) != rc::OK) {return ret;}}
#endif


namespace rc
{
    enum ReturnCode
    {
        END_OF_FILE = -1, // Scan iterator return codes may be negative
        UNKNOWN_FAILURE = 0x7FFFFFFF, // All custom error codes must be positive
        OK = 0,

        FEATURE_NOT_YET_IMPLEMENTED,

        FILE_PAGE_NOT_FOUND,
        FILE_SEEK_FAILED,
        FILE_NOT_FOUND,
        FILE_ALREADY_EXISTS,
        FILE_CORRUPT,
        FILE_COULD_NOT_OPEN,
        FILE_COULD_NOT_DELETE,
        FILE_NOT_OPENED,

        FILE_HANDLE_ALREADY_INITIALIZED,
        FILE_HANDLE_NOT_INITIALIZED,
        FILE_HANDLE_UNKNOWN,

        RECORD_DOES_NOT_EXIST,
        RECORD_CORRUPT,
        RECORD_SIZE_INVALID,
        RECORD_EXCEEDS_PAGE_SIZE,
		RECORD_DELETED,
        RECORD_IS_ANCHOR,

        HEADER_SIZE_CORRUPT,
        HEADER_PAGESIZE_MISMATCH,
        HEADER_VERSION_MISMATCH,
        HEADER_FREESPACE_LISTS_MISMATCH,
        HEADER_FREESPACE_LISTS_CORRUPT,
        HEADER_SIZE_TOO_LARGE,

        PAGE_CANNOT_BE_ORGANIZED,
		PAGE_NUM_INVALID,

		TABLE_NOT_FOUND,
		TABLE_ALREADY_CREATED,
		TABLE_NAME_TOO_LONG,

        ATTRIBUTE_INVALID_TYPE,
		ATTRIBUTE_NOT_FOUND,
		ATTRIBUTE_NAME_TOO_LONG,
		ATTRIBUTE_COUNT_MISMATCH,
		ATTRIBUTE_LENGTH_INVALID,

		BTREE_INDEX_PAGE_INITIALIZATION_FAILED,
        BTREE_INDEX_LEAF_ENTRY_NOT_FOUND,
        BTREE_INDEX_PAGE_FULL,
        BTREE_CANNOT_MERGE_PAGES_TOO_FULL,
        BTREE_CANNOT_MERGE_LEAF_AND_NONLEAF,
		BTREE_CANNOT_FIND_LEAF,

		OUT_OF_MEMORY
    };

    const char* rcToString(int rc);
}

#endif // _returncodes_h_
