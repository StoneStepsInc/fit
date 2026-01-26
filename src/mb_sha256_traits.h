#ifndef FIT_MB_SHA256_TRAITS_H
#define FIT_MB_SHA256_TRAITS_H

#include <isa-l_crypto/sha256_mb.h>

namespace fit {

struct mb_sha256_traits {
   typedef ISAL_SHA256_HASH_CTX_MGR HASH_CTX_MGR;
   typedef ISAL_SHA256_HASH_CTX HASH_CTX;

   static constexpr const char *HASH_TYPE = "SHA256";

   // isa-l_crypto packs hash bytes into uint32_t for some hashes (e.g. `12 34 56 78` is packed as `0x78563412`)
   static constexpr bool HASH_UINT32_REORDER = true;

   // hash size, in uint32_t values, as defined by isa-l_crypto
   static constexpr size_t HASH_UINT32_SIZE = ISAL_SHA256_DIGEST_NWORDS;

   // hash size, in bytes
   static constexpr size_t HASH_SIZE = HASH_UINT32_SIZE * sizeof(uint32_t);

   static constexpr int (*ctx_mgr_init)(HASH_CTX_MGR *mgr) = &isal_sha256_ctx_mgr_init;
   static constexpr int (*ctx_mgr_submit)(HASH_CTX_MGR *mgr, HASH_CTX *ctx, HASH_CTX **ctx_out, const void *buffer, uint32_t len, ISAL_HASH_CTX_FLAG flags) = &isal_sha256_ctx_mgr_submit;
   static constexpr int (*ctx_mgr_flush)(HASH_CTX_MGR *mgr, HASH_CTX **ctx_out) = &isal_sha256_ctx_mgr_flush;
};

}

#endif // FIT_MB_SHA256_TRAITS_H
