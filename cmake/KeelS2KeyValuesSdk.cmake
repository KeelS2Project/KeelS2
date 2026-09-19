# Keep the pinned SDK and public authoring headers unchanged. These private
# copies retain the SDK layout and packed symbol representation, while fixing
# hash alignment, detached rehash ownership, page bounds and table reallocation.
set(keels2_keyvalues_headers "${CMAKE_BINARY_DIR}/_keels2/keyvalues-sdk")
function(keels2_patch_keyvalues_header variable before after)
    string(FIND "${${variable}}" "${before}" position)

    if(position LESS 0)
        message(FATAL_ERROR "Pinned keyvalue SDK patch no longer applies")
    endif()

    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()
file(READ "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier1/utlsymbollarge.h" symbol)
file(READ "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier1/utlhashtable.h" hashtable)
file(READ "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier1/memblockallocator.h" memblock)
file(READ "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/tier1/keyvalues3.cpp" keyvalues)
file(READ "${KEELS2_SOURCE_SDK_RESOLVED_ROOT}/public/tier0/memdbgon.h" memdbgon)
string(REPLACE "\r\n" "\n" symbol "${symbol}")
string(REPLACE "\r\n" "\n" hashtable "${hashtable}")
string(REPLACE "\r\n" "\n" memblock "${memblock}")
string(REPLACE "\r\n" "\n" keyvalues "${keyvalues}")
string(REPLACE "\r\n" "\n" memdbgon "${memdbgon}")
keels2_patch_keyvalues_header(symbol
[=[	CUtlSymbolTableLargeBaseTreeEntry_t *entry = (CUtlSymbolTableLargeBaseTreeEntry_t *)m_MemBlockAllocator.GetBlock( block );

	entry->m_Hash = hash;
	char *pText = (char *)&entry->m_String[ 0 ];]=]
[=[	void *entry = m_MemBlockAllocator.GetBlock( block );
	memcpy( entry, &hash, sizeof( hash ) );
	char *pText = static_cast<char *>( entry ) + sizeof( LargeSymbolTableHashDecoration_t );]=])
keels2_patch_keyvalues_header(symbol
[=[	CUtlSymbolTableLargeBaseTreeEntry_t *entry = (CUtlSymbolTableLargeBaseTreeEntry_t *)m_MemBlockAllocator.GetBlock( m_MemBlocks[ id ] - sizeof( LargeSymbolTableHashDecoration_t ) );

	return entry->HashValue();]=]
[=[	LargeSymbolTableHashDecoration_t hash;
	memcpy( &hash, m_MemBlockAllocator.GetBlock( m_MemBlocks[ id ] - sizeof( LargeSymbolTableHashDecoration_t ) ), sizeof( hash ) );
	return hash;]=])
keels2_patch_keyvalues_header(hashtable
[=[	int nOldSize = m_nTableSize;

	if (!pOldBase)]=]
[=[	int nOldSize = m_nTableSize;
	TableT detachedOwner;
	if ( pOldBase ) detachedOwner.AssumeMemory( pOldBase, nOldSize );

	if (!pOldBase)]=])
file(CONFIGURE OUTPUT "${keels2_keyvalues_headers}/tier1/utlsymbollarge.h" CONTENT "@symbol@" @ONLY)
file(CONFIGURE OUTPUT "${keels2_keyvalues_headers}/tier1/utlhashtable.h" CONTENT "@hashtable@" @ONLY)
keels2_patch_keyvalues_header(memblock
[=[	if(nSize >= MaxPossiblePageSize())]=]
[=[	if( nSize > MaxPageSize() || static_cast<uint32>( m_MemPages.Count() ) >= MaxPossiblePageSize() )]=])
file(CONFIGURE OUTPUT "${keels2_keyvalues_headers}/tier1/memblockallocator.h" CONTENT "@memblock@" @ONLY)
keels2_patch_keyvalues_header(keyvalues
[=[		new_base = realloc( m_pDynamicBuffer, new_byte_size );

		memmove]=]
[=[		new_base = realloc( m_pDynamicBuffer, new_byte_size );
		m_pDynamicBuffer = new_base;

		memmove]=])
set(keels2_keyvalues_source "${keels2_keyvalues_headers}/sources/keyvalues3.cpp")
file(CONFIGURE OUTPUT "${keels2_keyvalues_source}" CONTENT "@keyvalues@" @ONLY)
set(keels2_keyvalues_patched_headers
    "${keels2_keyvalues_headers}/tier1/utlsymbollarge.h"
    "${keels2_keyvalues_headers}/tier1/utlhashtable.h"
    "${keels2_keyvalues_headers}/tier1/memblockallocator.h"
    "${keels2_keyvalues_headers}/tier0/memdbgon.h")
keels2_patch_keyvalues_header(memdbgon
[=[#if (defined(_DEBUG) || !defined(_INC_CRTDBG)) || defined(MEMDBGON_H)]=]
[=[#if 1]=])
file(CONFIGURE OUTPUT "${keels2_keyvalues_headers}/tier0/memdbgon.h" CONTENT "@memdbgon@" @ONLY)
