#ifndef FIT_MB_HASHER_H
#define FIT_MB_HASHER_H

#include <multi_buffer.h>

#include <optional>
#include <vector>
#include <tuple>
#include <queue>

namespace fit {

//
// Multi-buffer hasher based on Intel's isa-l_crypto.
//
template <typename mb_hash_traits, typename ... P>
class mb_hasher_t {
   public:
      typedef mb_hash_traits traits;

      typedef std::tuple<P...> param_tuple_t;

      //
      // From isa-l_crypto docs:
      //
      // > The SHA256_HASH_CTX_MGR object will be used to schedule
      // > processor resources, with up to 4 SHA256_HASH_CTX objects
      // > (or 8 in the AVX2 case, 16 in the AVX512) being processed
      // > at a time.
      // 
      // More contexts may be maintained than defined by these values,
      // but only the number of contexts allowed by the instruction
      // set selected at run time will be processed in parallel.
      //
      static constexpr size_t PAR_HASH_CTXS_SSE = 4;
      static constexpr size_t PAR_HASH_CTXS_AVX = 4;
      static constexpr size_t PAR_HASH_CTXS_AVX2 = 8;
      static constexpr size_t PAR_HASH_CTXS_AVX512 = 16;

      // align memory by this amount based on AVX512 where it is relevant
      static constexpr size_t ALIGN_MEM = 64;

   private:
      typedef std::vector<typename mb_hash_traits::HASH_CTX> hash_ctx_vec_t;

      typedef std::queue<typename mb_hash_traits::HASH_CTX*> hash_ctx_queue_t;

      typedef typename mb_hash_traits::HASH_CTX_MGR hash_ctx_mgr_t;

      struct ctx_args_t {
         // index into ctx_args_vec and mb_ctxs for this instance
         size_t id;

         // data buffer for get_data
         std::unique_ptr<unsigned char[]> buffer_storage;

         unsigned char *buffer;

         // arguments for get_data
         std::optional<param_tuple_t> params;

         // a caller-provided function to obtain the next data block for hashing
         bool (*get_data)(unsigned char *buffer, size_t buf_size, size_t& data_size, param_tuple_t& args);

         // total number of hashed bytes obtained via get_data
         size_t processed_size = 0;

         ctx_args_t(size_t id, size_t buf_size, param_tuple_t&& params, bool (*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&));
      };

   private:
      size_t buf_size;

      size_t max_ctxs;

      //
      // Data members in SHA256_MB_JOB_MGR must be aligned appropriately
      // for SSE, AVX, AVX2 and AVX512 operations or a crypting exception
      // will be thrown about not being able to access a memory location.
      // 
      // e.g. if SHA256_MB_JOB_MGR::lens is not aligned at least to a 16
      // byte boundary for AVX2, this instruction originating in the flush
      // call will throw an exception (rcx+280h points to `lens`).
      // 
      //    vmovdqa     xmm0,xmmword ptr [rcx+280h]
      // 
      // The value 64 is used because it is what's required for AVX512.
      // 
      // Worth noting that this alignment is a work-around and will not
      // hold if structures within hash manager change. A bug was created
      // for isa-l_crypto to align data members and depending on the
      // outcome this code will need to be adjusted.
      //
      alignas(ALIGN_MEM) hash_ctx_mgr_t mb_ctx_mgr;

      // isa-l_crypto hash job contexts
      hash_ctx_vec_t mb_ctxs;

      // storage container for job context arguments pointed to by mb_hash_traits::HASH_CTX::user_data in mb_ctxs
      std::vector<ctx_args_t> ctx_args_vec;

      // intermediately flushed contexts while looking for a completed one
      hash_ctx_queue_t flushed_ctxs;

      // indexes into mb_ctxs for mb_hash_traits::HASH_CTX instances with HASH_CTX_STS_COMPLETE status values
      std::vector<size_t> free_ctxs;

      // indexes into mb_ctxs for submitted jobs, before their 1st data block is hashed
      std::queue<size_t> pending_ctxs;

   public:
      mb_hasher_t(size_t buf_size, size_t max_jobs);

      mb_hasher_t(const mb_hasher_t&) = delete;
      mb_hasher_t(mb_hasher_t&&) = delete;

      ~mb_hasher_t(void);

      size_t max_jobs(void) const;

      size_t available_jobs(void) const;

      size_t active_jobs(void) const;

      template <typename ... O>
      void submit_job(param_tuple_t (*open_job)(O&&...), bool (*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&), O&&... param);

      std::optional<param_tuple_t> get_hash(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE]);

      static void isa_mb_hash_to_hex(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char hex_hash[mb_hash_traits::HASH_SIZE*2]);

      static void isa_mb_hash_to_bytes(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char bytes[mb_hash_traits::HASH_SIZE]);
};

}

#endif // FIT_MB_HASHER_H
