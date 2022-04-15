#include "sqlite.h"

#include <stdexcept>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {

const std::string sqlite_stmt_binder_t::err_null_stmt_msg = "SQLite statement must not be null";

sqlite_stmt_binder_t::sqlite_stmt_binder_t(sqlite3_stmt *stmt, const std::string_view& name) :
      stmt(stmt)
{
}

sqlite_stmt_binder_t::sqlite_stmt_binder_t(sqlite_stmt_binder_t&& other) :
      index(other.index),
      name(std::move(other.name)),
      stmt(other.stmt)
{
   other.stmt = nullptr;
}

sqlite_stmt_binder_t::~sqlite_stmt_binder_t(void)
{
   if(stmt)
      sqlite3_reset(stmt);
}

void sqlite_stmt_binder_t::reset(void)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg);

   int errcode;

   if((errcode = sqlite3_reset(stmt)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot reset statement ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::skip_param(void)
{
   ++index;
}

void sqlite_stmt_binder_t::bind_param(int64_t value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg);

   int errcode;

   if((errcode = sqlite3_bind_int64(stmt, ++index, value)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind int64 parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const std::string& value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg);

   int errcode;

   if((errcode = sqlite3_bind_text(stmt, ++index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind string parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const std::string_view& value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg);

   int errcode;

   if((errcode = sqlite3_bind_text(stmt, ++index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind string view parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const void *value, size_t size)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg);

   int errcode;

   if((errcode = sqlite3_bind_blob(stmt, ++index, value, static_cast<int>(size), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind binary parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

}
