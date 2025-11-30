#define NOMINMAX

#include "exif_reader.h"

#include "unicode.h"
#include "format.h"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/pointer.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <cinttypes>
#include <optional>
#include <string_view>
#include <cstring>
#include <cmath>
#include <algorithm>

#include <memory.h>

#include <exiv2/exiv2.hpp>

using namespace std::literals::string_view_literals;

namespace fit {
namespace exif {

//
// Exiv2 does not provide numeric constants for tags and string names
// are slower to process, so these constants are used instead. Their
// names and values are copied from libexif/0.6.24.
//
constexpr unsigned int EXIF_TAG_IMAGE_WIDTH           = 0x0100;
constexpr unsigned int EXIF_TAG_IMAGE_LENGTH          = 0x0101;
constexpr unsigned int EXIF_TAG_BITS_PER_SAMPLE       = 0x0102;
constexpr unsigned int EXIF_TAG_COMPRESSION           = 0x0103;
constexpr unsigned int EXIF_TAG_DOCUMENT_NAME         = 0x010d;
constexpr unsigned int EXIF_TAG_IMAGE_DESCRIPTION     = 0x010e;
constexpr unsigned int EXIF_TAG_MAKE                  = 0x010f;
constexpr unsigned int EXIF_TAG_MODEL                 = 0x0110;
constexpr unsigned int EXIF_TAG_ORIENTATION           = 0x0112;
constexpr unsigned int EXIF_TAG_SAMPLES_PER_PIXEL     = 0x0115;
constexpr unsigned int EXIF_TAG_SOFTWARE              = 0x0131;
constexpr unsigned int EXIF_TAG_DATE_TIME             = 0x0132;
constexpr unsigned int EXIF_TAG_ARTIST                = 0x013b;
constexpr unsigned int EXIF_TAG_XML_PACKET            = 0x02bc;
constexpr unsigned int EXIF_TAG_COPYRIGHT             = 0x8298;
constexpr unsigned int EXIF_TAG_EXPOSURE_TIME         = 0x829a;
constexpr unsigned int EXIF_TAG_FNUMBER               = 0x829d;
constexpr unsigned int EXIF_TAG_IMAGE_RESOURCES       = 0x8649;
constexpr unsigned int EXIF_TAG_EXPOSURE_PROGRAM      = 0x8822;
constexpr unsigned int EXIF_TAG_ISO_SPEED_RATINGS     = 0x8827;
constexpr unsigned int EXIF_TAG_TIME_ZONE_OFFSET      = 0x882a;
constexpr unsigned int EXIF_TAG_SENSITIVITY_TYPE      = 0x8830;
constexpr unsigned int EXIF_TAG_ISO_SPEED             = 0x8833;
constexpr unsigned int EXIF_TAG_DATE_TIME_ORIGINAL    = 0x9003;
constexpr unsigned int EXIF_TAG_DATE_TIME_DIGITIZED   = 0x9004;
constexpr unsigned int EXIF_TAG_OFFSET_TIME           = 0x9010;
constexpr unsigned int EXIF_TAG_OFFSET_TIME_ORIGINAL  = 0x9011;
constexpr unsigned int EXIF_TAG_OFFSET_TIME_DIGITIZED = 0x9012;
constexpr unsigned int EXIF_TAG_SHUTTER_SPEED_VALUE   = 0x9201;
constexpr unsigned int EXIF_TAG_APERTURE_VALUE        = 0x9202;
constexpr unsigned int EXIF_TAG_BRIGHTNESS_VALUE      = 0x9203;
constexpr unsigned int EXIF_TAG_EXPOSURE_BIAS_VALUE   = 0x9204;
constexpr unsigned int EXIF_TAG_MAX_APERTURE_VALUE    = 0x9205;
constexpr unsigned int EXIF_TAG_SUBJECT_DISTANCE      = 0x9206;
constexpr unsigned int EXIF_TAG_METERING_MODE         = 0x9207;
constexpr unsigned int EXIF_TAG_LIGHT_SOURCE          = 0x9208;
constexpr unsigned int EXIF_TAG_FLASH                 = 0x9209;
constexpr unsigned int EXIF_TAG_FOCAL_LENGTH          = 0x920a;
constexpr unsigned int EXIF_TAG_USER_COMMENT          = 0x9286;
constexpr unsigned int EXIF_TAG_MAKER_NOTE            = 0x927c;
constexpr unsigned int EXIF_TAG_SUB_SEC_TIME          = 0x9290;
constexpr unsigned int EXIF_TAG_SUB_SEC_TIME_ORIGINAL = 0x9291;
constexpr unsigned int EXIF_TAG_SUB_SEC_TIME_DIGITIZED = 0x9292;
constexpr unsigned int EXIF_TAG_FLASH_PIX_VERSION     = 0xa000;
constexpr unsigned int EXIF_TAG_FLASH_ENERGY          = 0xa20b;
constexpr unsigned int EXIF_TAG_SUBJECT_LOCATION      = 0xa214;
constexpr unsigned int EXIF_TAG_EXPOSURE_INDEX        = 0xa215;
constexpr unsigned int EXIF_TAG_SENSING_METHOD        = 0xa217;
constexpr unsigned int EXIF_TAG_SCENE_TYPE            = 0xa301;
constexpr unsigned int EXIF_TAG_EXPOSURE_MODE         = 0xa402;
constexpr unsigned int EXIF_TAG_WHITE_BALANCE         = 0xa403;
constexpr unsigned int EXIF_TAG_DIGITAL_ZOOM_RATIO    = 0xa404;
constexpr unsigned int EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM = 0xa405;
constexpr unsigned int EXIF_TAG_SCENE_CAPTURE_TYPE    = 0xa406;
constexpr unsigned int EXIF_TAG_SUBJECT_DISTANCE_RANGE = 0xa40c;
constexpr unsigned int EXIF_TAG_IMAGE_UNIQUE_ID       = 0xa420;
constexpr unsigned int EXIF_TAG_CAMERA_OWNER_NAME     = 0xa430;
constexpr unsigned int EXIF_TAG_BODY_SERIAL_NUMBER    = 0xa431;
constexpr unsigned int EXIF_TAG_LENS_SPECIFICATION    = 0xa432;
constexpr unsigned int EXIF_TAG_LENS_MAKE             = 0xa433;
constexpr unsigned int EXIF_TAG_LENS_MODEL            = 0xa434;
constexpr unsigned int EXIF_TAG_LENS_SERIAL_NUMBER    = 0xa435;

//
// These tags must only be looked up in the GPS IFD because their
// values overlap with EXIF tag values.
//
constexpr unsigned int EXIF_TAG_GPS_LATITUDE_REF      = 0x0001;
constexpr unsigned int EXIF_TAG_GPS_LATITUDE          = 0x0002;
constexpr unsigned int EXIF_TAG_GPS_LONGITUDE_REF     = 0x0003;
constexpr unsigned int EXIF_TAG_GPS_LONGITUDE         = 0x0004;
constexpr unsigned int EXIF_TAG_GPS_ALTITUDE_REF      = 0x0005;
constexpr unsigned int EXIF_TAG_GPS_ALTITUDE          = 0x0006;
constexpr unsigned int EXIF_TAG_GPS_TIME_STAMP        = 0x0007;
constexpr unsigned int EXIF_TAG_GPS_SPEED_REF         = 0x000c;
constexpr unsigned int EXIF_TAG_GPS_SPEED             = 0x000d;
constexpr unsigned int EXIF_TAG_GPS_DATE_STAMP        = 0x001d;

const char *exif_reader_t::jsonptr_oversized_expr = "/_fit/oversized";
const char *exif_reader_t::jsonptr_bad_utf8_expr = "/_fit/bad_utf8";

const char *exif_reader_t::jsonptr_push_back_expr = "/-";

// C++17 implies that the null character is preserved by "its length excluding the terminating null character"
const std::string_view exif_reader_t::whitespace = "\n \t\r"sv;

exif_reader_t::exif_reader_t(const options_t& options) :
      options(options),
      exif_fields(EXIF_FIELD_FieldCount),
      rapidjson_empty_array(rapidjson::kArrayType),
      jsonptr_oversized(jsonptr_oversized_expr),
      jsonptr_bad_utf8(jsonptr_bad_utf8_expr),
      jsonptr_push_back(jsonptr_push_back_expr)
{
}

exif_reader_t::exif_reader_t(exif_reader_t&& other) :
      options(other.options),
      exif_fields(std::move(other.exif_fields)),
      rapidjson_empty_array(rapidjson::kArrayType),
      jsonptr_oversized(jsonptr_oversized_expr),
      jsonptr_bad_utf8(jsonptr_bad_utf8_expr),
      jsonptr_push_back(jsonptr_push_back_expr)
{
}

//
// Formats a byte array within the field value as a set of space-separated
// values.
// 
// If formatted output is truncated, outputs `...` at the end of formatted
// string in the field value.
//
template <typename T>
void exif_reader_t::fmt_exif_byte(const Exiv2::DataValue& exif_value, const char *format, field_value_t& field_value)
{
   size_t slen;
   char8_t buf[128];

   slen = snprintf(reinterpret_cast<char*>(buf), sizeof(buf), format, static_cast<T>(exif_value.toUint32(0)));

   // append additional bytes until we run out of values or buffer is full
   for(size_t i = 1; i < exif_value.count() && slen < sizeof(buf)-1; i++) {
      *(buf+slen++) = ' ';
      slen += snprintf(reinterpret_cast<char*>(buf)+slen, sizeof(buf)-slen, format, static_cast<T>(exif_value.toUint32(i)));
   }

   // if snprintf indicated truncation, replace last 3 characters with `...` (there's a null character from snprintf)
   if(slen >= sizeof(buf)) {
      *(buf+sizeof(buf)-4) = '.';
      *(buf+sizeof(buf)-3) = '.';
      *(buf+sizeof(buf)-2) = '.';
   }

   field_value.emplace<std::u8string>(buf, slen >= sizeof(buf) ? sizeof(buf)-1 : slen);
}

//
// Converts a single numeric component to an `int64_t` and stores it within
// the field value. Formats multiple components as space-separated values
// within the field value.
//
// Returns true if an integer is returned for a single component or the
// formatted string wasn't truncated. Returns false if the formatted string
// would be truncated.
//
template <typename T>
bool exif_reader_t::fmt_exif_number(const Exiv2::ValueType<T>& exif_value, const char *format, field_value_t& field_value)
{
   size_t slen;
   char8_t buf[128];

   if(exif_value.count() == 1) {
      field_value = exif_value.value_.front();
      return true;
   }

   slen = snprintf(reinterpret_cast<char*>(buf), sizeof(buf), format, exif_value.value_.front());

   for(size_t i = 1; i < exif_value.count() && slen < sizeof(buf)-1; i++) {
      *(buf+slen++) = ' ';
      slen += snprintf(reinterpret_cast<char*>(buf)+slen, sizeof(buf)-slen, format, *(exif_value.value_.begin()+i));
   }

   // if snprintf indicated truncation, replace last 3 characters with `...` (there's a null character from snprintf)
   if(slen >= sizeof(buf)) {
      *(buf+sizeof(buf)-4) = '.';
      *(buf+sizeof(buf)-3) = '.';
      *(buf+sizeof(buf)-2) = '.';
   }

   field_value.emplace<std::u8string>(buf, slen >= sizeof(buf) ? sizeof(buf)-1 : slen);

   return slen < sizeof(buf);
}

//
// Formats rational numbers within the field value as a set of space-separated
// values.
// 
// Returns false if the formatted string would be truncated and true otherwise.
//
template <typename T>
bool exif_reader_t::fmt_exif_rational(const Exiv2::ValueType<T>& exif_value, field_value_t& field_value)
{
   size_t fsize;
   size_t slen = 0;
   char8_t buf[128];

   for(size_t i = 0; i < exif_value.count(); i++) {
      if(i) {
         if(sizeof(buf) - slen == 0)
            return false;

         *(buf+slen++) = u8' ';
      }

      T rational = *(exif_value.value_.begin() + i);

      int64_t numerator = static_cast<int64_t>(rational.first);
      int64_t denominator = static_cast<int64_t>(rational.second);

      if(!denominator)
         return false;

      // copied from libexif/0.6.24
      int decimals = (int)(log10(denominator)-0.08+1.0);

      if((fsize = snprintf(reinterpret_cast<char*>(buf)+slen, sizeof(buf)-slen, "%.*f",
            decimals,
            (double) numerator /
            (double) denominator)) >= sizeof(buf)-slen)
         return false;

      slen += fsize;
   }

   field_value.emplace<std::u8string>(buf, slen);

   return true;
}

std::u8string_view exif_reader_t::trim_whitespace(const std::u8string& value)
{
   return trim_whitespace(value.data(), value.size());
}

//
// This method is intended for entries typed as UNDEFINED and described
// as text entries, such as UserComment. Such entries are not required
// to be null-terminated, but may be padded heavily with null characters
// or whitespace. This method will trim whitespace from the beginning
// of the value and any padding from the end.
//
std::u8string_view exif_reader_t::trim_whitespace(const char8_t *value, size_t length)
{
   // we don't ever expect a null pointer in this call
   if(!value)
      throw std::runtime_error("Cannot trim whitespace against a null pointer");

   const char8_t *bp = value, *ep = bp + length;

   while (bp < ep && memchr(whitespace.data(), *bp, whitespace.length()))
      bp++;

   // include the null character in the set of whitespace characters (+1)
   while(ep > bp && memchr(whitespace.data(), *(ep-1), whitespace.length()+1))
      ep--;

   return std::u8string_view(bp, ep - bp);
}

//
// This method is intended for ASCII entries, which are null-terminated,
// but may have garbage following the null terminator. A good example
// of this is the LensModel in Canon's maker note block, which may have
// a value, such as "18-250mm", followed by a few dozen null characters
// and then a few non-null characters, all included into the entry size
// value.
//
std::u8string_view exif_reader_t::trim_whitespace(const char8_t *value)
{
   // we don't ever expect a null pointer in this call
   if(!value)
      throw std::runtime_error("Cannot trim whitespace against a null pointer");

   const char8_t *bp = value;

   while (*bp && memchr(whitespace.data(), *bp, whitespace.length()))
      bp++;

   return std::u8string_view(bp);
}

void exif_reader_t::initialize(print_stream_t& print_stream)
{
   if(!Exiv2::XmpParser::initialize())
      throw std::runtime_error("Cannot initialize the XMP parser library in Exiv2");
}

void exif_reader_t::cleanup(print_stream_t& print_stream) noexcept
{
   try {
      Exiv2::XmpParser::terminate();
   }
   catch (const std::exception& error) {
      print_stream.error("Cannot clean up XMP parser ({:s})\n", error.what());
   }
}

const std::string& exif_reader_t::get_exiv2_json_path(const char *family_name, const std::string& group_name, const std::string& tag_name)
{
   exiv2_json_path = "/";
   exiv2_json_path += family_name;
   exiv2_json_path += '/';
   exiv2_json_path += group_name;
   exiv2_json_path += '/';
   exiv2_json_path += tag_name;

   return exiv2_json_path;
}

template <typename T>
rapidjson::Value exif_reader_t::get_rational_array(const Exiv2::ValueType<T>& exif_value, size_t index)
{
   rapidjson::Value rational_parts(rapidjson::kArrayType);

   // [numerator, denominator]
   rational_parts.PushBack(exif_value.value_.at(index).first, rapidjson_mem_pool);
   rational_parts.PushBack(exif_value.value_.at(index).second, rapidjson_mem_pool);

   return rational_parts;
}

void exif_reader_t::update_exiv2_json(rapidjson::Document& exiv2_json, std::optional<Exiv2::IfdId> ifdId, std::optional<uint16_t> tagId, const char *family_name, const std::string& group_name, const std::string& tag_name, const Exiv2::Value& exif_value, const field_bitset_t& field_bitset)
{
   if(ifdId == Exiv2::IfdId::ifd0Id /* Image */) {
      // skip original uninterpreted XML packet with XMP values
      if(tagId == EXIF_TAG_XML_PACKET)
         return;
   }

   if(ifdId == Exiv2::IfdId::exifId) {
      // skip original uninterpreted maker notes block
      if(tagId == EXIF_TAG_MAKER_NOTE)
         return;
   }

   if(exif_value.size() > 0) {
      rapidjson::Pointer json_pointer(get_exiv2_json_path(family_name, group_name, tag_name));

      // process specific fields that require special handling
      if(ifdId == Exiv2::IfdId::exifId) {
         //
         // Exiv2 uses expensive methods to extract user comments (e.g.
         // a couple of substr, find(0)) and won't trim the whitespace,
         // so if we have a user comment in a field, use the field value.
         // If the field value is empty, it may be because the original
         // value contained only whitespace or invalid UTF-8 characters,
         // so for ASCII values always rely on the field value and
         // otherwise allow Exiv2 to perform the conversion.
         //
         if(tagId == EXIF_TAG_USER_COMMENT) {
            if(field_bitset.test(EXIF_FIELD_UserComment))
               json_pointer.Set(exiv2_json, rapidjson::Value(reinterpret_cast<const char*>(std::get<2>(exif_fields[EXIF_FIELD_UserComment]).c_str()), static_cast<rapidjson::SizeType>(std::get<2>(exif_fields[EXIF_FIELD_UserComment]).length()), rapidjson_mem_pool), rapidjson_mem_pool);
            else {
               const Exiv2::CommentValue *comment = dynamic_cast<const Exiv2::CommentValue*>(&exif_value);

               if(comment) {
                  if(!memcmp(comment->value_.data(), "ASCII\x0\x0\x0", 8)) {
                     //
                     // We may skip storing a comment value in exif_feilds if
                     // it contains only whitespace or at least one invalid UTF-8
                     // character. In order to detect the latter, we need to scan
                     // it one more time, which is simpler than tracking which of
                     // these cases we have with some state flag.
                     //
                     if(!unicode::is_valid_utf8(comment->value_.c_str()+8)) {
                        rapidjson::Value& bad_utf8_fields = jsonptr_bad_utf8.GetWithDefault(exiv2_json, rapidjson_empty_array, rapidjson_mem_pool);
                        jsonptr_push_back.Set(bad_utf8_fields, exiv2_json_path, rapidjson_mem_pool);
                     }
                  }
                  else {
                     // otherwise, allow Exiv2 to convert comment value to UTF-8
                     const std::string& comment_ref = comment->comment();

                     // need a reference to keep the string view valid against the temporary returned above
                     std::u8string_view comment_value = trim_whitespace(reinterpret_cast<const char8_t*>(comment_ref.c_str()), comment_ref.length());
                     if(!comment_value.empty())
                        json_pointer.Set(exiv2_json, rapidjson::Value(reinterpret_cast<const char*>(comment_value.data()), static_cast<rapidjson::SizeType>(comment_value.length()), rapidjson_mem_pool), rapidjson_mem_pool);
                  }
               }
            }
            return;
         }
         else if(tagId == EXIF_TAG_COPYRIGHT) {
            // keep the copyright field formatting consistent with the column value
            if(field_bitset.test(EXIF_FIELD_Copyright))
               json_pointer.Set(exiv2_json, rapidjson::Value(reinterpret_cast<const char*>(std::get<2>(exif_fields[EXIF_FIELD_Copyright]).c_str()), static_cast<rapidjson::SizeType>(std::get<2>(exif_fields[EXIF_FIELD_Copyright]).length()), rapidjson_mem_pool), rapidjson_mem_pool);
            return;
         }
      }

      // ASCII values are counted in bytes, not in values, so we need to process them explicitly
      if(exif_value.typeId() == Exiv2::TypeId::asciiString) {
         if(!unicode::is_valid_utf8(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str())) {
            rapidjson::Value& bad_utf8_fields = jsonptr_bad_utf8.GetWithDefault(exiv2_json, rapidjson_empty_array, rapidjson_mem_pool);
            jsonptr_push_back.Set(bad_utf8_fields, exiv2_json_path, rapidjson_mem_pool);
         }
         else {
            std::u8string_view ascii_value = trim_whitespace(reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));
            if(!ascii_value.empty())
               json_pointer.Set(exiv2_json, rapidjson::Value(reinterpret_cast<const char*>(ascii_value.data()), static_cast<rapidjson::SizeType>(ascii_value.length()), rapidjson_mem_pool));
         }
         return;
      }

      // same as ASCII values above
      if(exif_value.typeId() == Exiv2::TypeId::xmpText) {
         const Exiv2::XmpTextValue& xmp_text = static_cast<const Exiv2::XmpTextValue&>(exif_value);
         // we trust that the underlying XML contained valid UTF-8 sequences
         if(!xmp_text.value_.empty())
            json_pointer.Set(exiv2_json, rapidjson::Value(xmp_text.value_.data(), static_cast<rapidjson::SizeType>(xmp_text.value_.length()), rapidjson_mem_pool));
         return;
      }

      if(exif_value.count() == 1) {
         // output a single component value as a JSON value
         switch (exif_value.typeId()) {
            case Exiv2::TypeId::unsignedByte:
               [[ fallthrough ]];
            case Exiv2::TypeId::unsignedShort:
               [[ fallthrough ]];
            case Exiv2::TypeId::unsignedLong:
               json_pointer.Set(exiv2_json, static_cast<uint64_t>(exif_value.toUint32(static_cast<long>(0))));
               break;
            case Exiv2::TypeId::signedByte:
               [[ fallthrough ]];
            case Exiv2::TypeId::signedShort:
               [[ fallthrough ]];
            case Exiv2::TypeId::signedLong:
               json_pointer.Set(exiv2_json, static_cast<int64_t>(exif_value.toInt64(static_cast<long>(0))));
               break;
            case Exiv2::TypeId::signedRational:
               json_pointer.Set(exiv2_json, get_rational_array(static_cast<const Exiv2::ValueType<Exiv2::Rational>&>(exif_value), 0));
               break;
            case Exiv2::TypeId::unsignedRational:
               json_pointer.Set(exiv2_json, get_rational_array(static_cast<const Exiv2::ValueType<Exiv2::URational>&>(exif_value), 0));
               break;
            default:
               json_pointer.Set(exiv2_json, exif_value.toString(static_cast<long>(0)));
               break;
         }
      }
      else {
         //
         // Drop fields that have too many values, which typically will be
         // numeric arrays that are not easy to analyze and in most cases
         // will only take up space in the database. Keep track of the
         // fields we dropped.
         //
         if(exif_value.count() > MAX_JSON_ARRAY_SIZE) {
            // get the value of the dropped fields array (insert if if doesn't exist)
            rapidjson::Value& oversized_fields = jsonptr_oversized.GetWithDefault(exiv2_json, rapidjson_empty_array, rapidjson_mem_pool);

            // add the JSON pointer expression of the oversized field to /_fit/overiszed
            jsonptr_push_back.Set(oversized_fields, exiv2_json_path, rapidjson_mem_pool);
         }
         else {
            // output multiple component values as JSON arrays
            rapidjson::Value value_array(rapidjson::kArrayType);

            for(size_t i = 0; i < exif_value.count(); i++) {
               switch (exif_value.typeId()) {
                  case Exiv2::TypeId::undefined:
                     [[ fallthrough ]];
                  case Exiv2::TypeId::unsignedByte:
                     [[ fallthrough ]];
                  case Exiv2::TypeId::unsignedShort:
                     [[ fallthrough ]];
                  case Exiv2::TypeId::unsignedLong:
                     // EXIF's unsigned long is 32 bits and JSON numbers are 53 bits
                     value_array.PushBack(static_cast<uint32_t>(exif_value.toUint32(static_cast<long>(i))), rapidjson_mem_pool);
                     break;
                  case Exiv2::TypeId::signedByte:
                     [[ fallthrough ]];
                  case Exiv2::TypeId::signedShort:
                     [[ fallthrough ]];
                  case Exiv2::TypeId::signedLong:
                     // EXIF's long is 32 bits and JSON numbers are 53 bits
                     value_array.PushBack(static_cast<int64_t>(exif_value.toUint32(static_cast<long>(i))), rapidjson_mem_pool);
                     break;
                  case Exiv2::TypeId::signedRational:
                     value_array.PushBack(get_rational_array(static_cast<const Exiv2::ValueType<Exiv2::Rational>&>(exif_value), i), rapidjson_mem_pool);
                     break;
                  case Exiv2::TypeId::unsignedRational:
                     value_array.PushBack(get_rational_array(static_cast<const Exiv2::ValueType<Exiv2::URational>&>(exif_value), i), rapidjson_mem_pool);
                     break;
                  default:
                     //
                     // PushBack allocates a local variable for the value and uses
                     // a constructor without the allocator, which won't match the
                     // constructor taking std::string, so we construct our own
                     // value instead and move into the array.
                     //
                     value_array.PushBack(rapidjson::Value(exif_value.toString(static_cast<long>(i)), rapidjson_mem_pool), rapidjson_mem_pool);
                     break;
               }
            }

            json_pointer.Set(exiv2_json, value_array);
         }
      }
   }
}

field_bitset_t exif_reader_t::read_file_exif(const std::filesystem::path& filepath, print_stream_t& print_stream)
{
   try {
      field_bitset_t field_bitset;

      std::optional<rapidjson::Document> exiv2_json;

      if(options.exiv2_json)
         exiv2_json.emplace(&rapidjson_mem_pool);

#ifdef _WIN32
      Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(filepath);
#else
      Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(filepath.string(), false /* useCurl? */);
#endif

      if(!image)
         return field_bitset;

      image->readMetadata();

      const Exiv2::ExifData& exif_data = image->exifData();

      if(!exif_data.count())
         return field_bitset;

      // used only for numeric formatting - strings are copied directly from entries
      char8_t buf[64];

      // field index within the bitset; set only for fields that should be processed according to their type
      std::optional<size_t> field_index;

      //
      // Walk all tags found in a file and assign a field index based
      // on the tag value. This works out faster than calling Exiv2
      // for each tag (e.g. ExifData.findKey) because it loops through
      // tags to find the specified one.
      // 
      // The loop below consists of two sections. The top one formats
      // tags that are less useful if formated in a generic way, such
      // as the f-number or the exposure time values, and the bottom
      // section formats tag values generically, according to their
      // type.
      // 
      // Exiv2 formatting is not used because it uses string streams,
      // which are slower, and we also want data consistency in the
      // storage in case if Exiv2 formatting changes in the future.
      // 
      for(Exiv2::ExifData::const_iterator i = exif_data.begin(); i != exif_data.end(); ++i) {

         //
         // Ignore all IFDs except main image (1), EXIF (5) and GPS (6)
         // if we are not collecting JSON metadata.
         // 
         if(!options.exiv2_json) {
            if(i->ifdId() != Exiv2::IfdId::ifd0Id && i->ifdId() != Exiv2::IfdId::exifId && i->ifdId() != Exiv2::IfdId::gpsId)
               continue;
         }

         //
         // Exiv2 doesn't provide a check for a null value and just throws
         // an exception from value() or clones the value in getValue().
         // We don't want either and will use invalid type ID as an
         // indicator for a null value.
         //
         if(i->typeId() != Exiv2::invalidTypeId) {
            const Exiv2::Value& exif_value = i->value();

            field_index.reset();

            // GPS tags overlap with ExifTag values and cannot be in the same switch
            if(i->ifdId() == Exiv2::IfdId::gpsId) {
               switch(i->tag()) {
                  case EXIF_TAG_GPS_LATITUDE_REF:
                     field_index = EXIF_FIELD_GPSLatitudeRef;
                     break;
                  case EXIF_TAG_GPS_LATITUDE:
                     field_index = EXIF_FIELD_GPSLatitude;
                     break;
                  case EXIF_TAG_GPS_LONGITUDE_REF:
                     field_index = EXIF_FIELD_GPSLongitudeRef;
                     break;
                  case EXIF_TAG_GPS_LONGITUDE:
                     field_index = EXIF_FIELD_GPSLongitude;
                     break;
                  case EXIF_TAG_GPS_ALTITUDE_REF:
                     field_index = EXIF_FIELD_GPSAltitudeRef;
                     break;
                  case EXIF_TAG_GPS_ALTITUDE:
                     field_index = EXIF_FIELD_GPSAltitude;
                     break;
                  case EXIF_TAG_GPS_TIME_STAMP:
                     // in rational format - will look like "12,34,56" for 12:34:56
                     field_index = EXIF_FIELD_GPSTimeStamp;
                     break;
                  case EXIF_TAG_GPS_SPEED_REF:
                     field_index = EXIF_FIELD_GPSSpeedRef;
                     break;
                  case EXIF_TAG_GPS_SPEED:
                     field_index = EXIF_FIELD_GPSSpeed;
                     break;
                  case EXIF_TAG_GPS_DATE_STAMP:
                     //
                     // Use less-than 11 comparison, in case if the value is zero-padded
                     // to a larger size. Keep shorter values or values with unrecognized
                     // format as-is (will be processed by the field type `switch` below).
                     // 
                     //     4  7
                     // YYYY:MM:DD\x0
                     if(exif_value.typeId() != Exiv2::TypeId::asciiString || exif_value.size() < 11 || exif_value.toUint32(4) != static_cast<uint32_t>(':') || exif_value.toUint32(7) != static_cast<uint32_t>(':'))
                        field_index = EXIF_FIELD_GPSDateStamp;
                     else {
                        std::u8string& tstamp = std::get<std::u8string>(exif_fields[EXIF_FIELD_GPSDateStamp] = reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));
                        tstamp[4] = tstamp[7] = u8'-';
                        field_bitset.set(EXIF_FIELD_GPSDateStamp);
                     }
                     break;
                  default:
                     break;
               }
            }
            else {
               // 0 (main image), 5 (EXIF)
               switch(i->tag()) {
                  case EXIF_TAG_BITS_PER_SAMPLE:
                     field_index = EXIF_FIELD_BitsPerSample;
                     break;
                  case EXIF_TAG_COMPRESSION:
                     field_index = EXIF_FIELD_Compression;
                     break;
                  case EXIF_TAG_DOCUMENT_NAME:
                     field_index = EXIF_FIELD_DocumentName;
                     break;
                  case EXIF_TAG_IMAGE_DESCRIPTION:
                     field_index = EXIF_FIELD_ImageDescription;
                     break;
                  case EXIF_TAG_MAKE:
                     field_index = EXIF_FIELD_Make;
                     break;
                  case EXIF_TAG_MODEL:
                     field_index = EXIF_FIELD_Model;
                     break;
                  case EXIF_TAG_ORIENTATION:
                     field_index = EXIF_FIELD_Orientation;
                     break;
                  case EXIF_TAG_SAMPLES_PER_PIXEL:
                     field_index = EXIF_FIELD_SamplesPerPixel;
                     break;
                  case EXIF_TAG_SOFTWARE:
                     field_index = EXIF_FIELD_Software;
                     break;
                  case EXIF_TAG_DATE_TIME:
                     //     4  7
                     // YYYY:MM:DD HH:MM:SS\x0
                     if(exif_value.typeId() != Exiv2::TypeId::asciiString || exif_value.size() < 20 || exif_value.toUint32(4) != static_cast<uint32_t>(':') || exif_value.toUint32(7) != static_cast<uint32_t>(':'))
                        field_index = EXIF_FIELD_DateTime;
                     else {
                        // assign a C-string to avoid picking up padding, if there is any
                        std::u8string& tstamp = std::get<std::u8string>(exif_fields[EXIF_FIELD_DateTime] = reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));
                        tstamp[4] = tstamp[7] = u8'-';
                        field_bitset.set(EXIF_FIELD_DateTime);
                     }
                     break;
                  case EXIF_TAG_ARTIST:
                     field_index = EXIF_FIELD_Artist;
                     break;
                  case EXIF_TAG_COPYRIGHT:
                     if(exif_value.typeId() != Exiv2::TypeId::asciiString)
                        field_index = EXIF_FIELD_Copyright;
                     else {
                        //
                        // may be one of 3 forms:
                        // 
                        //   * photographer copyright \x0 editor copyright \x0
                        //   * photographer copyright \x0
                        //   * \x20\x0 editor copyright \x0
                        //
                        // Note that the entire value is validated using field length,
                        // including a possible null character in the middle, which
                        // does not invalidate the string.
                        //
                        if(exif_value.size() > 1 && unicode::is_valid_utf8(static_cast<const Exiv2::AsciiValue&>(exif_value).value_)) {
                           // do not trim the whitespace because the space in front is significant
                           std::u8string_view copyright_value = reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str());

                           if(!copyright_value.empty()) {
                              std::u8string& copyright = exif_fields[EXIF_FIELD_Copyright].emplace<std::u8string>(copyright_value);

                              // search for the editor copyright separator and replace it with a semicolon
                              size_t editor_sep = copyright.find_first_of(u8'\x0');
                              if(editor_sep != std::string::npos)
                                 *(copyright.begin() + editor_sep) = ';';

                              field_bitset.set(EXIF_FIELD_Copyright);
                           }
                        }
                     }
                     break;
                  case EXIF_TAG_EXPOSURE_TIME:
                     if(exif_value.typeId() != Exiv2::TypeId::unsignedRational || exif_value.count() != 1)
                        field_index = EXIF_FIELD_ExposureTime;
                     else {
                        const Exiv2::ValueType<Exiv2::URational>& rational = static_cast<const Exiv2::ValueType<Exiv2::URational>&>(i->value());

                        if(!rational.value_.front().second)
                           field_index = EXIF_FIELD_ExposureTime;
                        else {
                           double value = (double) rational.value_.front().first / (double) rational.value_.front().second;

                           if(!value || value >= 1)
                              field_index = EXIF_FIELD_ExposureTime;
                           else {
                              size_t slen = snprintf(reinterpret_cast<char*>(buf), sizeof(buf), "1/%.0f", 1. / value);

                              exif_fields[EXIF_FIELD_ExposureTime].emplace<std::u8string>(buf, slen);

                              field_bitset.set(EXIF_FIELD_ExposureTime);
                           }
                        }
                     }
                     break;
                  case EXIF_TAG_FNUMBER:
                     if(exif_value.typeId() != Exiv2::TypeId::unsignedRational || exif_value.count() != 1)
                        field_index = EXIF_FIELD_FNumber;
                     else {
                        const Exiv2::ValueType<Exiv2::URational>& rational = static_cast<const Exiv2::ValueType<Exiv2::URational>&>(i->value());

                        if(!rational.value_.front().second)
                           field_index = EXIF_FIELD_FNumber;
                        else {
                           size_t slen = snprintf(reinterpret_cast<char*>(buf), sizeof(buf), "f/%.01f", (double) rational.value_.front().first / (double) rational.value_.front().second);

                           exif_fields[EXIF_FIELD_FNumber].emplace<std::u8string>(buf, slen);

                           field_bitset.set(EXIF_FIELD_FNumber);
                        }
                     }
                     break;
                  case EXIF_TAG_EXPOSURE_PROGRAM:
                     field_index = EXIF_FIELD_ExposureProgram;
                     break;
                  case EXIF_TAG_ISO_SPEED_RATINGS:
                     // renamed to PhotographicSensitivity in later EXIF versions
                     field_index = EXIF_FIELD_ISOSpeedRatings;
                     break;
                  case EXIF_TAG_TIME_ZONE_OFFSET:
                     field_index = EXIF_FIELD_TimeZoneOffset;
                     break;
                  case EXIF_TAG_SENSITIVITY_TYPE:
                     field_index = EXIF_FIELD_SensitivityType;
                     break;
                  case EXIF_TAG_ISO_SPEED:
                     field_index = EXIF_FIELD_ISOSpeed;
                     break;
                  case EXIF_TAG_DATE_TIME_ORIGINAL:
                     //     4  7
                     // YYYY:MM:DD HH:MM:SS\x0
                     if(exif_value.typeId() != Exiv2::TypeId::asciiString || exif_value.size() < 20 || exif_value.toUint32(4) != static_cast<uint32_t>(':') || exif_value.toUint32(7) != static_cast<uint32_t>(':'))
                        field_index = EXIF_FIELD_DateTimeOriginal;
                     else {
                        std::u8string& tstamp = std::get<std::u8string>(exif_fields[EXIF_FIELD_DateTimeOriginal] = reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));
                        tstamp[4] = tstamp[7] = '-';
                        field_bitset.set(EXIF_FIELD_DateTimeOriginal);
                     }
                     break;
                  case EXIF_TAG_DATE_TIME_DIGITIZED:
                     //     4  7
                     // YYYY:MM:DD HH:MM:SS\x0
                     if(exif_value.typeId() != Exiv2::TypeId::asciiString || exif_value.size() < 20 || exif_value.toUint32(4) != static_cast<uint32_t>(':') || exif_value.toUint32(7) != static_cast<uint32_t>(':'))
                        field_index = EXIF_FIELD_DateTimeDigitized;
                     else {
                        std::u8string& tstamp = std::get<std::u8string>(exif_fields[EXIF_FIELD_DateTimeDigitized] = reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));
                        tstamp[4] = tstamp[7] = '-';
                        field_bitset.set(EXIF_FIELD_DateTimeDigitized);
                     }
                     break;
                  case EXIF_TAG_OFFSET_TIME:
                     field_index = EXIF_FIELD_OffsetTime;
                     break;
                  case EXIF_TAG_OFFSET_TIME_ORIGINAL:
                     field_index = EXIF_FIELD_OffsetTimeOriginal;
                     break;
                  case EXIF_TAG_OFFSET_TIME_DIGITIZED:
                     field_index = EXIF_FIELD_OffsetTimeDigitized;
                     break;
                  case EXIF_TAG_SHUTTER_SPEED_VALUE:
                     // Tv = -log2(ExposureTime)
                     field_index = EXIF_FIELD_ShutterSpeedValue;
                     break;
                  case EXIF_TAG_MAX_APERTURE_VALUE:
                     // Av = 2 * log2(FNumber)
                     field_index = EXIF_FIELD_MaxApertureValue;
                     break;
                  case EXIF_TAG_APERTURE_VALUE:
                     // Av = 2 * log2(FNumber)
                     field_index = EXIF_FIELD_ApertureValue;
                     break;
                  case EXIF_TAG_BRIGHTNESS_VALUE:
                     field_index = EXIF_FIELD_BrightnessValue;
                     break;
                  case EXIF_TAG_EXPOSURE_BIAS_VALUE:
                     field_index = EXIF_FIELD_ExposureBiasValue;
                     break;
                  case EXIF_TAG_SUBJECT_DISTANCE:
                     field_index = EXIF_FIELD_SubjectDistance;
                     break;
                  case EXIF_TAG_METERING_MODE:
                     field_index = EXIF_FIELD_MeteringMode;
                     break;
                  case EXIF_TAG_LIGHT_SOURCE:
                     field_index = EXIF_FIELD_LightSource;
                     break;
                  case EXIF_TAG_FLASH:
                     field_index = EXIF_FIELD_Flash;
                     break;
                  case EXIF_TAG_FOCAL_LENGTH:
                     field_index = EXIF_FIELD_FocalLength;
                     break;
                  case EXIF_TAG_USER_COMMENT:
                     //
                     // The first 8 bytes identify the character set, but it isn't well
                     // designed and there is no byte order defined for the "Unicode"
                     // identifier, which may be different from the EXIF byte order).
                     // We can only interpret ASCII intelligently, which will also
                     // work for UTF-8. Other encoding types are ignored. Note that
                     // the field itself has the type UNDEFINED, so the string isn't
                     // null-terminated.
                     // 
                     if(exif_value.size() > 8) { 
                        const Exiv2::CommentValue *comment = dynamic_cast<const Exiv2::CommentValue*>(&i->value());

                        //
                        // Avoid Exiv2::CommentValue::charsetId(), which is unnessesarily
                        // expensive because it is calling substr() and creating string
                        // temporaries, and check the underlying EXIF data formatted as
                        // a user comment field.
                        //
                        if(comment && !memcmp(comment->value_.data(), "ASCII\x0\x0\x0", 8)) {
                           //
                           // This field is typed as undefined, but must be padded with
                           // zeros, so it may appear as if it's null-terminated, but
                           // it's not. That is, if field size is 16 and there's 8 user
                           // comment characters (plus the character set indicator),
                           // there will be no null terminator.
                           //
                           size_t padding_pos = comment->value_.find_first_of('\x0', 8);

                           //
                           // Some files identify as ASCII, but store UCS-2, which would
                           // put an empty string into the database. Some images contain
                           // only spaces or line feed characters. Trim all whitespace
                           // from both ends and if anything is left, store it in the
                           // field.
                           //
                           if(*(comment->value_.begin()+8) && (padding_pos == std::string::npos || padding_pos > 8)) {
                              if(unicode::is_valid_utf8(comment->value_.data()+8, padding_pos == std::string::npos ? comment->value_.length()-8 : padding_pos-8)) {
                                 std::u8string_view comment_value = trim_whitespace(reinterpret_cast<const char8_t*>(comment->value_.data())+8, padding_pos == std::string::npos ? comment->value_.length()-8 : padding_pos-8);

                                 // skip anything less than two characters (big endian UCS-2 will be interepted as a single character)
                                 if(comment_value.length() > 1) {
                                    exif_fields[EXIF_FIELD_UserComment].emplace<std::u8string>(comment_value);
                                    field_bitset.set(EXIF_FIELD_UserComment);
                                 }
                              }
                           }
                        }
                     }
                     break;
                  case EXIF_TAG_SUB_SEC_TIME:
                     field_index = EXIF_FIELD_SubsecTime;
                     break;
                  case EXIF_TAG_SUB_SEC_TIME_ORIGINAL:
                     field_index = EXIF_FIELD_SubSecTimeOriginal;
                     break;
                  case EXIF_TAG_SUB_SEC_TIME_DIGITIZED:
                     field_index = EXIF_FIELD_SubSecTimeDigitized;
                     break;
                  case EXIF_TAG_FLASH_PIX_VERSION:
                     field_index = EXIF_FIELD_FlashpixVersion;
                     break;
                  case EXIF_TAG_FLASH_ENERGY:
                     field_index = EXIF_FIELD_FlashEnergy;
                     break;
                  case EXIF_TAG_SUBJECT_LOCATION:
                     field_index = EXIF_FIELD_SubjectLocation;
                     break;
                  case EXIF_TAG_EXPOSURE_INDEX:
                     field_index = EXIF_FIELD_ExposureIndex;
                     break;
                  case EXIF_TAG_SENSING_METHOD:
                     field_index = EXIF_FIELD_SensingMethod;
                     break;
                  case EXIF_TAG_SCENE_TYPE:
                     field_index = EXIF_FIELD_SceneType;
                     break;
                  case EXIF_TAG_EXPOSURE_MODE:
                     field_index = EXIF_FIELD_ExposureMode;
                     break;
                  case EXIF_TAG_WHITE_BALANCE:
                     field_index = EXIF_FIELD_WhiteBalance;
                     break;
                  case EXIF_TAG_DIGITAL_ZOOM_RATIO:
                     field_index = EXIF_FIELD_DigitalZoomRatio;
                     break;
                  case EXIF_TAG_FOCAL_LENGTH_IN_35MM_FILM:
                     field_index = EXIF_FIELD_FocalLengthIn35mmFilm;
                     break;
                  case EXIF_TAG_SCENE_CAPTURE_TYPE:
                     field_index = EXIF_FIELD_SceneCaptureType;
                     break;
                  case EXIF_TAG_SUBJECT_DISTANCE_RANGE:
                     field_index = EXIF_FIELD_SubjectDistanceRange;
                     break;
                  case EXIF_TAG_IMAGE_UNIQUE_ID:
                     field_index = EXIF_FIELD_ImageUniqueID;
                     break;
                  case EXIF_TAG_CAMERA_OWNER_NAME:
                     field_index = EXIF_FIELD_CameraOwnerName;
                     break;
                  case EXIF_TAG_BODY_SERIAL_NUMBER:
                     field_index = EXIF_FIELD_BodySerialNumber;
                     break;
                  case EXIF_TAG_LENS_SPECIFICATION:
                     field_index = EXIF_FIELD_LensSpecification;
                     break;
                  case EXIF_TAG_LENS_MAKE:
                     field_index = EXIF_FIELD_LensMake;
                     break;
                  case EXIF_TAG_LENS_MODEL:
                     field_index = EXIF_FIELD_LensModel;
                     break;
                  case EXIF_TAG_LENS_SERIAL_NUMBER:
                     field_index = EXIF_FIELD_LensSerialNumber;
                     break;
                  default:
                     break;
               }
            }

            if(field_index.has_value() && exif_value.size()) {
               switch(exif_value.typeId()) {
                  case Exiv2::TypeId::unsignedByte:
                     fmt_exif_byte<unsigned char>(static_cast<const Exiv2::DataValue&>(exif_value), "%02hhx", exif_fields[field_index.value()]);
                     field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::asciiString:
                     //
                     // Some of the ASCII entries have padding included in the entry
                     // size, so if we just assign std::string, that padding will be
                     // stored in the database and may produce unexpected results,
                     // such as camera make stored as "Samsung\x0\x0" being different
                     // from "Samsung\x0". Use c_str() to avoid this, which is slower,
                     // because we need to scan each string for a null character, but
                     // yields meaningful results.
                     //
                     // ASCII entries are always null-terminated (some may have null character embedded in the string - e.g. Copyright)
                     if(exif_value.size() > 1 && unicode::is_valid_utf8(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str())) {
                        std::u8string_view ascii_value = trim_whitespace(reinterpret_cast<const char8_t*>(static_cast<const Exiv2::AsciiValue&>(exif_value).value_.c_str()));

                        if(!ascii_value.empty()) {
                           exif_fields[field_index.value()].emplace<std::u8string>(ascii_value);
                           field_bitset.set(field_index.value());
                        }
                     }
                     break;
                  case Exiv2::TypeId::unsignedShort:
                     if(fmt_exif_number(static_cast<const Exiv2::ValueType<uint16_t>&>(exif_value), "%hu", exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::unsignedLong:
                     if(fmt_exif_number(static_cast<const Exiv2::ValueType<uint32_t>&>(exif_value), "%" PRIu32, exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::unsignedRational:
                     if(fmt_exif_rational(static_cast<const Exiv2::ValueType<Exiv2::URational>&>(exif_value), exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::signedRational:
                     if(fmt_exif_rational(static_cast<const Exiv2::ValueType<Exiv2::Rational>&>(exif_value), exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::signedByte:
                     fmt_exif_byte<char>(static_cast<const Exiv2::DataValue&>(exif_value), "%hhd", exif_fields[field_index.value()]);
                     field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::undefined:
                     //
                     // This format type is can be anything, ranging from one byte
                     // (e.g. scene type) to four bytes (e.g. flash PIX version) to
                     // arbitrary structures (e.g. MakerNote). Those fields we know
                     // about should be formatted in the section above and here we
                     // can only output values as hex data, which may be truncated.
                     //
                     fmt_exif_byte<unsigned char>(static_cast<const Exiv2::DataValue&>(exif_value), "%02hhx", exif_fields[field_index.value()]);
                     field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::signedShort:
                     if(fmt_exif_number(static_cast<const Exiv2::ValueType<int16_t>&>(exif_value), "%hd", exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  case Exiv2::TypeId::signedLong:
                     if(fmt_exif_number(static_cast<const Exiv2::ValueType<int32_t>&>(exif_value), "%" PRId32, exif_fields[field_index.value()]))
                        field_bitset.set(field_index.value());
                     break;
                  default:
                     //
                     // There are a few more types we don't expect for the tags we
                     // are capturing, such as these. See Exiv2::TypeId for a full
                     // list.
                     // 
                     // Exiv2::TypeId::tiffDouble
                     // Exiv2::TypeId::unsignedLongLong
                     //
                     break;
               }
            }
         }

         if(exiv2_json.has_value())
            update_exiv2_json(exiv2_json.value(), i->ifdId(), i->tag(), i->familyName(), i->groupName(), i->tagName(), i->value(), field_bitset);
      }

      if(!image->xmpData().empty()) {
         //
         // XMP value key will include the XML namespace and will have
         // the `Xmp.` prefix, so for this XMP element:
         // 
         //   <xmp:Rating>5</xmp:Rating>
         //
         // , this key will be returned. There is no tag numbers in XMP.
         // 
         //   Xmp.xmp.Rating (XmpText,1): 5
         // 
         // For the purposes of avoiding conflicts with other tag names
         // and with similar tags within XMP, use the lowercase XMP
         // namespace between XMP and capitalized tag name, such as these:
         // 
         //   XMPxmpRating
         //   XMPdcSubject
         // 
         // We are only interested in xmp.Rating at this point and break
         // out as soon as it is found to avoid unnecessary iterations.
         //
         for(Exiv2::XmpData::const_iterator i = image->xmpData().begin(); i != image->xmpData().end(); ++i) {

            if(i->key() == "Xmp.xmp.Rating"sv) {
               //
               // According to this ExifTool page, xmp.Rating is supposed to
               // be a floating point number, but it is retrieved as XmpText
               // in Exiv2, so we'll keep it simple and store it as-is.
               // 
               // https://exiftool.org/TagNames/XMP.html#xmp
               //
               if(i->typeId() == Exiv2::TypeId::xmpText) {
                  const Exiv2::XmpTextValue& xmp_text = static_cast<const Exiv2::XmpTextValue&>(i->value());
                  exif_fields[EXIF_FIELD_XMPxmpRating].emplace<std::u8string>(std::u8string_view(reinterpret_cast<const char8_t*>(xmp_text.value_.c_str()), xmp_text.value_.length()));
                  field_bitset.set(EXIF_FIELD_XMPxmpRating);
               }
            }

         if(exiv2_json.has_value())
            update_exiv2_json(exiv2_json.value(), std::nullopt, std::nullopt, i->familyName(), i->groupName(), i->tagName(), i->value(), field_bitset);
         }
      }

      if(exiv2_json.has_value()) {
         // check against an empty document if we added any fields
         if(exiv2_json != rapidjson::Document(&rapidjson_mem_pool)) {
            rapidjson::StringBuffer strbuf;
            rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);

            exiv2_json.value().Accept(writer);

            exif_fields[EXIF_FIELD_Exiv2Json] = reinterpret_cast<const char8_t*>(strbuf.GetString());
            field_bitset.set(EXIF_FIELD_Exiv2Json);
         }
      }

      return field_bitset;
   }
   catch (const std::exception& error) {
      print_stream.error("Cannot read EXIF for {:s} ({:s})\n", filepath.u8string(), error.what());
   }
   catch (...) {
      print_stream.error("Cannot read EXIF for {:s}\n", filepath.u8string());
   }

   // return an empty bitset and discard a partially filled one
   return field_bitset_t{};
}

const std::vector<field_value_t>& exif_reader_t::get_exif_fields(void) const
{
   return exif_fields;
}

}
}
