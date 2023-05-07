#include "mb_hasher.h"

namespace fit {

template <typename mb_hash_traits, typename ... P>
mb_hasher_t<mb_hash_traits, P...>::ctx_args_t::ctx_args_t(size_t id, size_t buf_size, param_tuple_t&& params, bool (*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&)) :
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

template <typename mb_hash_traits, typename ... P>
mb_hasher_t<mb_hash_traits, P...>::mb_hasher_t(size_t buf_size, size_t max_ctxs) :
      buf_size(buf_size),
      max_ctxs(max_ctxs)
{
   mb_hash_traits::ctx_mgr_init(&mb_ctx_mgr);

   // cannot reallocate either of these vectors or user data pointer will end up dangling
   mb_ctxs.reserve(max_ctxs);
   ctx_args_vec.reserve(max_ctxs);

   // preallocate enough memory for this instance's lifetime
   free_ctxs.reserve(max_ctxs);
}

template <typename mb_hash_traits, typename ... P>
mb_hasher_t<mb_hash_traits, P...>::~mb_hasher_t(void)
{
}

template <typename mb_hash_traits, typename ... P>
template <typename ... O>
void mb_hasher_t<mb_hash_traits, P...>::submit_job(param_tuple_t (*open_job)(O&&...), bool (*get_data)(unsigned char*, size_t, size_t&, param_tuple_t&), O&&... param)
{
   ctx_args_t *ctx_args = nullptr;

   if(!free_ctxs.empty()) {
      ctx_args = static_cast<ctx_args_t*>(mb_ctxs[free_ctxs.back()].user_data);

      free_ctxs.pop_back();

      if(mb_ctxs[ctx_args->id].status != HASH_CTX_STS_COMPLETE || ctx_args->params.has_value())
         throw std::runtime_error("A free context must be completed and cannot have arguments (" + std::to_string(ctx_args->id) + ")");

      ctx_args->params = open_job(std::forward<O>(param)...);
      ctx_args->get_data = get_data;
   }
   else {
      if(mb_ctxs.size() == max_ctxs)
         throw std::runtime_error("Maximum number of multi-buffer contexts has been reached");

      param_tuple_t oparams = open_job(std::forward<O>(param)...);

      ctx_args = &ctx_args_vec.emplace_back(mb_ctxs.size(), buf_size, std::move(oparams), get_data);

      typename mb_hash_traits::HASH_CTX *mb_ctx_ptr = &mb_ctxs.emplace_back();

      // sets mb_ctx_ptr->status to HASH_CTX_STS_COMPLETE
      hash_ctx_init(mb_ctx_ptr);

      // store a pointer to the corresponding data context
      mb_ctx_ptr->user_data = ctx_args;
   }

   pending_ctxs.push(ctx_args->id);
}

template <typename mb_hash_traits, typename ... P>
std::optional<typename mb_hasher_t<mb_hash_traits, P...>::param_tuple_t> mb_hasher_t<mb_hash_traits, P...>::get_hash(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE])
{
   typename mb_hash_traits::HASH_CTX *mb_ctx_ptr = nullptr;

   bool moredata = false;

   size_t data_size = 0;

   size_t last_block_done = 0;

   while (!last_block_done) {
      if(mb_ctx_ptr) {
         // always process the immediate context for as long as we have it
         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         moredata = ctx_args->get_data(ctx_args->buffer, buf_size, data_size, ctx_args->params.value());

         mb_ctx_ptr = mb_hash_traits::ctx_mgr_submit(&mb_ctx_mgr, mb_ctx_ptr, ctx_args->buffer, static_cast<uint32_t>(data_size), moredata ? HASH_UPDATE : HASH_LAST);

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
         mb_ctx_ptr = &mb_ctxs[pending_ctxs.back()];

         pending_ctxs.pop();

         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         if(mb_ctx_ptr->status != HASH_CTX_STS_COMPLETE || !ctx_args->params.has_value() || ctx_args->processed_size)
            throw std::runtime_error("Got a bad pending state for a job " + std::to_string(ctx_args->id));

         moredata = ctx_args->get_data(ctx_args->buffer, buf_size, data_size, ctx_args->params.value());

         mb_ctx_ptr = mb_hash_traits::ctx_mgr_submit(&mb_ctx_mgr, mb_ctx_ptr, ctx_args->buffer, static_cast<uint32_t>(data_size), moredata ? HASH_FIRST : HASH_ENTIRE);

         ctx_args->processed_size += data_size;

         if(!moredata)
            last_block_done++;
      }
      else {
         // finally, see if we can flush a context to continue
         if((mb_ctx_ptr = mb_hash_traits::ctx_mgr_flush(&mb_ctx_mgr)) == nullptr)
            throw std::runtime_error("Got a null flushed context while processing jobs");
      }

      if(mb_ctx_ptr) {
         ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

         if(mb_ctx_ptr->error != HASH_CTX_ERROR_NONE)
            throw std::runtime_error("Got a context with an error (" + std::to_string(mb_ctx_ptr->error) + ") for a job " + std::to_string(ctx_args->id));

         if(mb_ctx_ptr->status == HASH_CTX_STS_COMPLETE) {
            ctx_args->processed_size = 0;
            std::optional<param_tuple_t> params = std::move(ctx_args->params);
            ctx_args->params.reset();

            memcpy(isa_mb_hash, mb_ctx_ptr->job.result_digest, mb_hash_traits::HASH_SIZE);

            free_ctxs.push_back(ctx_args->id);
            return params;
         }
      }
   }

   while((mb_ctx_ptr = mb_hash_traits::ctx_mgr_flush(&mb_ctx_mgr)) != nullptr) {
      ctx_args_t *ctx_args = static_cast<ctx_args_t*>(mb_ctx_ptr->user_data);

      if(mb_ctx_ptr->error != HASH_CTX_ERROR_NONE)
         throw std::runtime_error("Got a flushed context with an error (" + std::to_string(mb_ctx_ptr->error) + ") for a job " + std::to_string(ctx_args->id));

      //
      // All returned contexts must be continued and cannot be discarded
      // (i.e. they will not be returned again if flushed again). Hold
      // onto these contexts in the order we got them from flush.
      //
      if(mb_ctx_ptr->status != HASH_CTX_STS_COMPLETE)
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

   throw std::runtime_error("Got a null flushed context while finalizing jobs");
}

template <typename mb_hash_traits, typename ... P>
void mb_hasher_t<mb_hash_traits, P...>::isa_mb_hash_to_hex(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char hex_hash[mb_hash_traits::HASH_SIZE*2])
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

template <typename mb_hash_traits, typename ... P>
void mb_hasher_t<mb_hash_traits, P...>::isa_mb_hash_to_bytes(uint32_t isa_mb_hash[mb_hash_traits::HASH_UINT32_SIZE], unsigned char bytes[mb_hash_traits::HASH_SIZE])
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
