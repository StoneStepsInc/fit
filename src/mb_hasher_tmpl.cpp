#include "mb_hasher.h"

#include <memory>

namespace fit {

template <typename mb_hash_traits, typename T, typename ... P>
mb_hasher_t<mb_hash_traits, T, P...>::ctx_args_t::ctx_args_t(size_t id, size_t buf_size, param_tuple_t&& params, bool (T::*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&) const noexcept) :
      id(id),
      buffer_storage(new unsigned char [buf_size+ALIGN_MEM]),
      buffer(buffer_storage.get()),
      get_data(get_data),
      params(std::move(params)),
      processed_size(0)
{
   size_t buf_space = buf_size+ALIGN_MEM;

   // no need to keep buf_space - we just need the buffer to have buf_size bytes
   if(std::align(ALIGN_MEM, buf_size, reinterpret_cast<void*&>(buffer), buf_space) == nullptr)
      throw std::runtime_error("Cannot align a memory buffer for hashing");
}

template <typename mb_hash_traits, typename T, typename ... P>
mb_hasher_t<mb_hash_traits, T, P...>::mb_hasher_t(const T& data_obj, size_t buf_size, size_t max_jobs) :
      data_obj(data_obj),
      buf_size(buf_size),
      max_ctxs(max_jobs)
{
   mb_hash_traits::ctx_mgr_init(&mb_ctx_mgr);

   // cannot reallocate either of these vectors or user data pointer will end up dangling
   mb_ctxs.reserve(max_ctxs);
   ctx_args_vec.reserve(max_ctxs);

   // preallocate enough memory for this instance's lifetime
   free_ctxs.reserve(max_ctxs);
}

template <typename mb_hash_traits, typename T, typename ... P>
mb_hasher_t<mb_hash_traits, T, P...>::~mb_hasher_t(void)
{
}

template <typename mb_hash_traits, typename T, typename ... P>
size_t mb_hasher_t<mb_hash_traits, T, P...>::max_jobs(void) const
{
   return max_ctxs;
}

template <typename mb_hash_traits, typename T, typename ... P>
size_t mb_hasher_t<mb_hash_traits, T, P...>::available_jobs(void) const
{
   return max_ctxs - mb_ctxs.size() + free_ctxs.size();
}

template <typename mb_hash_traits, typename T, typename ... P>
size_t mb_hasher_t<mb_hash_traits, T, P...>::active_jobs(void) const
{
   return mb_ctxs.size() - free_ctxs.size();
}

//
// `open_job` and `get_data` are pointers to member functions, which
// are expected to be `const` and are intended to provide read-only
// context for hash jobs. All mutable hash job data, such as data
// stream handle, position, amount of bytes read so far, etc, should
// be maintained in hash job parameters in `param_tuple_t`.
// 
// `open_job` may throw an exception, in which case there will be
// no change in the `mb_hasher_t` state. The caller is responsible
// in this case for handling the exception.
// 
// `read_data` is not allowed to throw exceptions. If any error is
// encountered, it should be carried in the parameter tuple and
// `read_data` should return `false` to indicate that there is no
// more data. The caller is responsible for checking if there is
// an error in the returned parameter tuple and handle such error
// appropriately.
// 
// Note that the hash value in case of an error should be considered
// as meaningless, even though it will contain a hash of data returned
// by `read_data` up until the error was encountered (including the
// data size set in the last call that returned `false` to indicate
// that there's no more data).
//
template <typename mb_hash_traits, typename T, typename ... P>
template <typename ... O>
void mb_hasher_t<mb_hash_traits, T, P...>::submit_job(param_tuple_t (T::*open_job)(O&&...) const, bool (T::*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&) const noexcept, O&&... param)
{
   ctx_args_t *ctx_args = nullptr;

   if(!free_ctxs.empty()) {
      ctx_args = static_cast<ctx_args_t*>(mb_ctxs[free_ctxs.back()].user_data);

      // we cannot throw away a bad context, so this becomes an unrecoverable error that will need to be tracked as a bug
      if(mb_ctxs[ctx_args->id].status != ISAL_HASH_CTX_STS_COMPLETE || ctx_args->params.has_value())
         throw std::runtime_error("A free context must be completed and cannot have arguments (" + std::to_string(ctx_args->id) + ")");

      ctx_args->params = (data_obj.*open_job)(std::forward<O>(param)...);
      ctx_args->get_data = get_data;

      //
      // If open_job throws an exception, the context will remain in
      // free_ctxs with the same status and without params. It is the
      // responsibility of the caller to catch this exception and
      // ensure that it does not affect other active jobs, which must
      // be completed for this class to remain usable.
      //
      free_ctxs.pop_back();
   }
   else {
      if(mb_ctxs.size() == max_ctxs)
         throw std::runtime_error("Maximum number of multi-buffer contexts has been reached");

      param_tuple_t oparams = (data_obj.*open_job)(std::forward<O>(param)...);

      ctx_args = &ctx_args_vec.emplace_back(mb_ctxs.size(), buf_size, std::move(oparams), get_data);

      typename mb_hash_traits::HASH_CTX *mb_ctx_ptr = &mb_ctxs.emplace_back();

      // sets mb_ctx_ptr->status to HASH_CTX_STS_COMPLETE
      isal_hash_ctx_init(mb_ctx_ptr);

      // store a pointer to the corresponding data context
      mb_ctx_ptr->user_data = ctx_args;
   }

   pending_ctxs.push(ctx_args->id);
}

template <typename mb_hash_traits, typename T, typename ... P>
std::optional<typename mb_hasher_t<mb_hash_traits, T, P...>::param_tuple_t> mb_hasher_t<mb_hash_traits, T, P...>::get_hash(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE])
{
   typename mb_hash_traits::HASH_CTX *mb_ctx_ptr = nullptr;

   bool moredata = false;

   size_t data_size = 0;

   size_t last_block_done = 0;

   int isal_error = ISAL_CRYPTO_ERR_NONE;

   while (mb_ctx_ptr || !last_block_done) {
      if(mb_ctx_ptr) {
         // always process the immediate context for as long as we have it
         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         moredata = (data_obj.*ctx_args->get_data)(ctx_args->buffer, buf_size, data_size, ctx_args->params.value());

         if((isal_error = mb_hash_traits::ctx_mgr_submit(&mb_ctx_mgr, mb_ctx_ptr, &mb_ctx_ptr, ctx_args->buffer, static_cast<uint32_t>(data_size), moredata ? ISAL_HASH_UPDATE : ISAL_HASH_LAST)) != ISAL_CRYPTO_ERR_NONE)
            throw std::runtime_error(FMTNS::format("Cannot submit a hash job {:s} ({:d})", std::to_string(ctx_args->id), isal_error));

         ctx_args->processed_size += data_size;

         if(!moredata)
            last_block_done++;
      }
      else if(!flushed_ctxs.empty()) {
         // process flushed contexts before starting new ones, in the order we got them flushed
         mb_ctx_ptr = flushed_ctxs.front();
         flushed_ctxs.pop();
      }
      else if(!pending_ctxs.empty()) {
         // add a new pending job into the mix (order doesn't matter here)
         mb_ctx_ptr = &mb_ctxs[pending_ctxs.front()];

         pending_ctxs.pop();

         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         if(mb_ctx_ptr->status != ISAL_HASH_CTX_STS_COMPLETE || !ctx_args->params.has_value() || ctx_args->processed_size)
            throw std::runtime_error("Got a bad pending state for a hash job " + std::to_string(ctx_args->id));

         moredata = (data_obj.*ctx_args->get_data)(ctx_args->buffer, buf_size, data_size, ctx_args->params.value());

         //
         // We may get here zero-length data and it's too late to back out
         // because the job has already been submitted. isa-l_crypto seems
         // to handle these cases gracefully, but caller should discard the
         // resulting hash.
         //
         if((isal_error = mb_hash_traits::ctx_mgr_submit(&mb_ctx_mgr, mb_ctx_ptr, &mb_ctx_ptr, ctx_args->buffer, static_cast<uint32_t>(data_size), moredata ? ISAL_HASH_FIRST : ISAL_HASH_ENTIRE)) != ISAL_CRYPTO_ERR_NONE)
            throw std::runtime_error(FMTNS::format("Cannot submit a hash job {:s} ({:d})", std::to_string(ctx_args->id), isal_error));

         ctx_args->processed_size += data_size;

         if(!moredata)
            last_block_done++;
      }
      else {
         // finally, see if we can flush a context to continue
         if(mb_hash_traits::ctx_mgr_flush(&mb_ctx_mgr, &mb_ctx_ptr) || mb_ctx_ptr == nullptr)
            throw std::runtime_error("Got a null flushed context while processing hash jobs");
      }

      if(mb_ctx_ptr) {
         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         if(mb_ctx_ptr->error != ISAL_HASH_CTX_ERROR_NONE)
            throw std::runtime_error("Got a context with an error (" + std::to_string(mb_ctx_ptr->error) + ") for a hash job " + std::to_string(ctx_args->id));

         if(mb_ctx_ptr->status == ISAL_HASH_CTX_STS_COMPLETE) {
            ctx_args->processed_size = 0;
            std::optional<param_tuple_t> params = std::move(ctx_args->params);
            ctx_args->params.reset();

            memcpy(isa_mb_hash, mb_ctx_ptr->job.result_digest, mb_hash_traits::HASH_SIZE);

            free_ctxs.push_back(ctx_args->id);
            return params;
         }
      }
   }

   while((isal_error = mb_hash_traits::ctx_mgr_flush(&mb_ctx_mgr, &mb_ctx_ptr)) == ISAL_CRYPTO_ERR_NONE && mb_ctx_ptr != nullptr) {
      ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

      if(mb_ctx_ptr->error != ISAL_HASH_CTX_ERROR_NONE)
         throw std::runtime_error("Got a flushed context with an error (" + std::to_string(mb_ctx_ptr->error) + ") for a hash job " + std::to_string(ctx_args->id));

      //
      // All returned contexts must be continued and cannot be discarded
      // (i.e. they will not be returned again if flushed again). Hold
      // onto these contexts in the order we got them from flush.
      //
      if(mb_ctx_ptr->status != ISAL_HASH_CTX_STS_COMPLETE)
         flushed_ctxs.push(mb_ctx_ptr);
      else {
         ctx_args->processed_size = 0;
         std::optional<param_tuple_t> params = std::move(ctx_args->params);
         ctx_args->params.reset();

         memcpy(isa_mb_hash, mb_ctx_ptr->job.result_digest, mb_hash_traits::HASH_SIZE);

         free_ctxs.push_back(ctx_args->id);
         return params;
      }
   }

   if(isal_error != ISAL_CRYPTO_ERR_NONE)
      throw std::runtime_error(FMTNS::format("Cannot flush a hash job ({:d})", isal_error));
   else
      throw std::runtime_error("Got a null flushed context while finalizing hash jobs");
}

template <typename mb_hash_traits, typename T, typename ... P>
void mb_hasher_t<mb_hash_traits, T, P...>::isa_mb_hash_to_hex(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char hex_hash[mb_hash_traits::HASH_SIZE*2])
{
   static const char hex[] = "0123456789abcdef";

   for(size_t i = 0; i < mb_hash_traits::HASH_SIZE; i++) {
      if(mb_hash_traits::HASH_UINT32_REORDER) {
         // reposition little endian uint32_t bytes into a byte sequence (i.e. 0th -> 3rd, 1st -> 2nd, etc) and convert to hex
         hex_hash[((i - i % sizeof(uint32_t)) + (sizeof(uint32_t) - i % sizeof(uint32_t) - 1)) * 2] = hex[(*(reinterpret_cast<unsigned char*>(isa_mb_hash)+i) & 0xF0) >> 4];
         hex_hash[(((i - i % sizeof(uint32_t)) + (sizeof(uint32_t) - i % sizeof(uint32_t) - 1)) * 2) + 1] = hex[*(reinterpret_cast<unsigned char*>(isa_mb_hash)+i) & 0x0F];
      }
      else {
         hex_hash[i*2] = hex[(*(reinterpret_cast<unsigned char*>(isa_mb_hash)+i) & 0xF0) >> 4];
         hex_hash[i*2+1] = hex[*(reinterpret_cast<unsigned char*>(isa_mb_hash)+i) & 0x0F];
      }
   }
}

template <typename mb_hash_traits, typename T, typename ... P>
void mb_hasher_t<mb_hash_traits, T, P...>::isa_mb_hash_to_bytes(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char bytes[mb_hash_traits::HASH_SIZE])
{
   if(mb_hash_traits::HASH_UINT32_REORDER) {
      // hash bytes are packed as little endian uint32_t elements
      for(size_t i = 0; i < mb_hash_traits::HASH_SIZE; i++) {
         // reposition little endian uint32_t bytes into a byte sequence (i.e. 0th -> 3rd, 1st -> 2nd, etc)
         *(bytes+((i - i % sizeof(uint32_t)) + (sizeof(uint32_t) - i % sizeof(uint32_t) - 1))) = *(reinterpret_cast<unsigned char*>(isa_mb_hash)+i);
      }
   }
}

}
