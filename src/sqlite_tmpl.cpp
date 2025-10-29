#include "sqlite.h"

#include <optional>
#include <type_traits>
#include <stdexcept>

namespace fit {

template <typename T>
struct is_std_optional : std::false_type {};

template <typename T>
struct is_std_optional<std::optional<T>> : std::true_type {};

template <typename T>
T get_sqlite_field_value(sqlite3_stmt *stmt, int column)
{
   if(sqlite3_column_type(stmt, column) == SQLITE_NULL) {
      if constexpr (is_std_optional<T>::value)
         return std::nullopt;
      else
         throw std::logic_error("A NULL is found for a non-optional field type");
   }

   if constexpr (std::is_same<T, int>::value)
      return sqlite3_column_int(stmt, column);

   else if constexpr (std::is_same<T, std::optional<int>>::value)
      return std::make_optional(sqlite3_column_int(stmt, column));

   else if constexpr (std::is_same<T, int64_t>::value)
      return sqlite3_column_int64(stmt, column);

   else if constexpr (std::is_same<T, std::optional<int64_t>>::value)
      return std::make_optional(sqlite3_column_int64(stmt, column));

   else if constexpr (std::is_same<T, double>::value)
      return sqlite3_column_double(stmt, column);

   else if constexpr (std::is_same<T, std::optional<double>>::value)
      return std::make_optional(sqlite3_column_double(stmt, column));

   else if constexpr (std::is_same<T, std::string>::value)
      return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, column)), static_cast<size_t>(sqlite3_column_bytes(stmt, column)));

   else if constexpr (std::is_same<T, std::optional<std::string>>::value)
      return std::make_optional<std::string>(reinterpret_cast<const char*>(sqlite3_column_text(stmt, column)), static_cast<size_t>(sqlite3_column_bytes(stmt, column)));

   else
      throw std::logic_error("Unexpected SQLite column value type");
}

//
// sqlite_record_t
//

template <typename ...T>
sqlite_record_t<T...>::sqlite_record_t(sqlite3_stmt *stmt) :
      fields(make_fields_tuple(stmt, std::make_index_sequence<sizeof...(T)>{}))
{
}

template <typename ...T>
template <std::size_t ...I>
std::tuple<T...> sqlite_record_t<T...>::make_fields_tuple(sqlite3_stmt *stmt, std::index_sequence<I...>)
{
    return std::make_tuple(get_sqlite_field_value<T>(stmt, static_cast<int>(I))...);
}

template <typename ...T>
const std::tuple<T...>& sqlite_record_t<T...>::get_fields(void) const
{
   return fields;
}

template <typename ...T>
template<size_t I>
const typename std::tuple_element<I, std::tuple<T...>>::type& sqlite_record_t<T...>::get_field(void) const
{
   return std::get<I>(fields);
}

}
