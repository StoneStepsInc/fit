#ifndef FIT_SQLITE_H
#define FIT_SQLITE_H

#include <sqlite3.h>

#include <string>
#include <string_view>

namespace fit {

template <typename T>
struct sqlite_malloc_deleter_t {
   void operator ()(T *ptr)
   {
      sqlite3_free(ptr);
   }
};

//
// A SQLite statement parameter binder class.
//
// In addition to binding parameters, this class also works as a
// scoped SQLite statement bind/reset wrapper. Note that SQLite's
// statement reset leaves bound parameters intact and just releases
// statement resources and locks acquired after the last execution.
//
class sqlite_stmt_binder_t {
   int index = 0;                // parameter 1-based binding index

   std::string name;             // statement name for error reporting purposes

   sqlite3_stmt *stmt;           // SQLite statement

   public:
      sqlite_stmt_binder_t(sqlite3_stmt *stmt, const std::string_view& name);

      sqlite_stmt_binder_t(sqlite_stmt_binder_t&& other);

      ~sqlite_stmt_binder_t(void);

      void reset(void);

      void skip_param(void);

      void bind_param(int64_t value);

      void bind_param(const std::string& value);

      void bind_param(const std::string_view& value);

      void bind_param(const void *value, size_t size);
};

//
// A SQLite statement convenience wrapper class.
//
class sqlite_stmt_t {
   std::string name;             // statement name for error reporting purposes

   sqlite3_stmt *stmt;           // SQLite statement

   public:
      sqlite_stmt_t(const std::string_view& name);

      sqlite_stmt_t(sqlite_stmt_t&& other);

      ~sqlite_stmt_t(void);

      operator bool (void) const;

      operator sqlite3_stmt* (void);

      operator const sqlite3_stmt* (void) const;

      int prepare(sqlite3 *db, const std::string_view& sql);

      int prepare(sqlite3 *db, std::string_view sql, std::string_view& sqltail);

      int finalize(void);
};

}

#endif // FIT_SQLITE_H
