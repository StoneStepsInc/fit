#ifndef FIT_MB_SHA256_TRAITS_H
#define FIT_MB_SHA256_TRAITS_H

#include <sha256_mb.h>

namespace fit {

struct mb_sha256_traits {
   typedef SHA256_HASH_CTX_MGR HASH_CTX_MGR;
   typedef SHA256_HASH_CTX HASH_CTX;

   static constexpr const char *HASH_TYPE = "SHA256";

   // isa-l_crypto packs hash bytes into uint32_t for some hashes (e.g. `12 34 56 78` is packed as `0x78563412`)
   static constexpr bool HASH_UINT32_REORDER = true;

   // hash size, in uint32_t values, as defined by isa-l_crypto
   static constexpr size_t HASH_UINT32_SIZE = SHA256_DIGEST_NWORDS;

   // hash size, in bytes
   static constexpr size_t HASH_SIZE = HASH_UINT32_SIZE * sizeof(uint32_t);

   static constexpr void (*ctx_mgr_init)(HASH_CTX_MGR *mgr) = &sha256_ctx_mgr_init;
   static constexpr HASH_CTX* (*ctx_mgr_submit)(HASH_CTX_MGR *mgr, HASH_CTX *ctx, const void *buffer, uint32_t len, HASH_CTX_FLAG flags) = &sha256_ctx_mgr_submit;
   static constexpr HASH_CTX* (*ctx_mgr_flush)(HASH_CTX_MGR *mgr) = &sha256_ctx_mgr_flush;
};

}

#endif // FIT_MB_SHA256_TRAITS_H
