#include "sqlite.h"

#include <stdexcept>

using namespace std::literals::string_view_literals;
using namespace std::literals::string_literals;

namespace fit {

static const std::string err_null_stmt_msg = "SQLite statement must not be null";
static const std::string err_reuse_stmt_msg = "SQLite statement must finalized before it can be reused";

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
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   int errcode;

   if((errcode = sqlite3_reset(stmt)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot reset statement ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::skip_param(void)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   ++index;
}

void sqlite_stmt_binder_t::bind_param(int64_t value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   int errcode;

   if((errcode = sqlite3_bind_int64(stmt, ++index, value)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind int64 parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const std::string& value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   int errcode;

   if((errcode = sqlite3_bind_text(stmt, ++index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind string parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const std::string_view& value)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   int errcode;

   if((errcode = sqlite3_bind_text(stmt, ++index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind string view parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

void sqlite_stmt_binder_t::bind_param(const void *value, size_t size)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   int errcode;

   if((errcode = sqlite3_bind_blob(stmt, ++index, value, static_cast<int>(size), SQLITE_TRANSIENT)) != SQLITE_OK)
      throw std::runtime_error(name + ": cannot bind binary parameter ("s + sqlite3_errstr(errcode) + ")"s);
}

//
// sqlite_stmt_t
//

sqlite_stmt_t::sqlite_stmt_t(const std::string_view& name) :
      name(name),
      stmt(nullptr)
{
}

sqlite_stmt_t::sqlite_stmt_t(sqlite_stmt_t&& other) :
      name(std::move(other.name)),
      stmt(other.stmt)
{
   other.stmt = nullptr;
}

sqlite_stmt_t::~sqlite_stmt_t(void)
{
   if(stmt)
      sqlite3_finalize(stmt);
}

sqlite_stmt_t::operator bool (void) const
{
   return stmt != nullptr;
}

sqlite_stmt_t::operator sqlite3_stmt* (void)
{
   return stmt;
}

sqlite_stmt_t::operator const sqlite3_stmt* (void) const
{
   return stmt;
}

int sqlite_stmt_t::prepare(sqlite3 *db, const std::string_view& sql)
{
   if(stmt)
      throw std::runtime_error(err_reuse_stmt_msg + "(" + name + ")");

   return sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
}

//
// This method may be used to execute batches of statements, one
// statement at a time. Every time this method is called, the
// first SQL statement in a batch is prepared and sqltail is set
// to hold the rest of the batch, which can be passed into this
// method again, once the original statement is finalized.
//
int sqlite_stmt_t::prepare(sqlite3 *db, std::string_view sql, std::string_view& sqltail)
{
   const char *tail = nullptr;

   if(stmt)
      throw std::runtime_error(err_reuse_stmt_msg + "(" + name + ")");

   int retval = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);

   // stmt may be NULL if all we have is a non-SQL trailer
   if(tail)
      sqltail = std::string_view(tail, sql.size() - (tail - sql.data()));
   else
      sqltail = std::string_view();

   return retval;
}

int sqlite_stmt_t::finalize(void)
{
   if(!stmt)
      throw std::runtime_error(err_null_stmt_msg + "(" + name + ")");

   sqlite3_stmt *stmt = this->stmt;
   this->stmt = nullptr;
   
   return sqlite3_finalize(stmt);
}

}
