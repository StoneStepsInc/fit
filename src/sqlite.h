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
// scoped SQLite statement binde/reset wrapper. Note that SQLite's
// statement reset leaves bound parameters intact and instead
// releases statement resources and locks acquired after the last
// execution.
//
class sqlite_stmt_binder_t {
   static const std::string err_null_stmt_msg;

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

}

#endif // FIT_SQLITE_H
