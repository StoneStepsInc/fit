#ifndef FIT_MB_SHA1_TRAITS_H
#define FIT_MB_SHA1_TRAITS_H

#include <isa-l_crypto/sha1_mb.h>

namespace fit {

struct mb_sha1_traits {
   typedef SHA1_HASH_CTX_MGR HASH_CTX_MGR;
   typedef SHA1_HASH_CTX HASH_CTX;

   static constexpr const char *HASH_TYPE = "SHA1";

   static constexpr bool HASH_UINT32_REORDER = true;

   static constexpr size_t HASH_UINT32_SIZE = SHA1_DIGEST_NWORDS;

   static constexpr size_t HASH_SIZE = HASH_UINT32_SIZE * sizeof(uint32_t);

   static constexpr void (*ctx_mgr_init)(HASH_CTX_MGR *mgr) = &sha1_ctx_mgr_init;
   static constexpr HASH_CTX* (*ctx_mgr_submit)(HASH_CTX_MGR *mgr, HASH_CTX *ctx, const void *buffer, uint32_t len, HASH_CTX_FLAG flags) = &sha1_ctx_mgr_submit;
   static constexpr HASH_CTX* (*ctx_mgr_flush)(HASH_CTX_MGR *mgr) = &sha1_ctx_mgr_flush;
};

}

#endif // FIT_MB_SHA1_TRAITS_H
