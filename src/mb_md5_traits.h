#ifndef FIT_MB_MD5_TRAITS_H
#define FIT_MB_MD5_TRAITS_H

#include <md5_mb.h>

namespace fit {

struct mb_md5_traits {
   typedef MD5_HASH_CTX_MGR HASH_CTX_MGR;
   typedef MD5_HASH_CTX HASH_CTX;

   static constexpr const char *HASH_TYPE = "MD5";

   static constexpr bool HASH_UINT32_REORDER = false;

   static constexpr size_t HASH_UINT32_SIZE = MD5_DIGEST_NWORDS;

   static constexpr size_t HASH_SIZE = HASH_UINT32_SIZE * sizeof(uint32_t);

   static constexpr void (*ctx_mgr_init)(HASH_CTX_MGR *mgr) = &md5_ctx_mgr_init;
   static constexpr HASH_CTX* (*ctx_mgr_submit)(HASH_CTX_MGR *mgr, HASH_CTX *ctx, const void *buffer, uint32_t len, HASH_CTX_FLAG flags) = &md5_ctx_mgr_submit;
   static constexpr HASH_CTX* (*ctx_mgr_flush)(HASH_CTX_MGR *mgr) = &md5_ctx_mgr_flush;
};

}

#endif // FIT_MB_MD5_TRAITS_H
