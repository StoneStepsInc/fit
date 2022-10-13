#include "exif_reader.h"

#include <cinttypes>
#include <optional>
#include <cstring>
#include <cmath>

#include <memory.h>

#include <libexif/exif-tag.h>
#include <libexif/exif-data.h>

namespace fit {
namespace exif {

exif_reader_t::exif_reader_t(void) :
      EXIF_SIZE_RATIONAL(exif_format_get_size(EXIF_FORMAT_RATIONAL)),
      EXIF_SIZE_SRATIONAL(exif_format_get_size(EXIF_FORMAT_SRATIONAL)),
      EXIF_SIZE_SHORT(exif_format_get_size(EXIF_FORMAT_SHORT)),
      EXIF_SIZE_SSHORT(exif_format_get_size(EXIF_FORMAT_SSHORT)),
      EXIF_SIZE_LONG(exif_format_get_size(EXIF_FORMAT_LONG)),
      EXIF_SIZE_SLONG(exif_format_get_size(EXIF_FORMAT_SLONG)),
      exif_fields(EXIF_FIELD_FieldCount)
{
}

exif_reader_t::exif_reader_t(exif_reader_t&& other) :
      EXIF_SIZE_RATIONAL(other.EXIF_SIZE_RATIONAL),
      EXIF_SIZE_SRATIONAL(other.EXIF_SIZE_SRATIONAL),
      EXIF_SIZE_SHORT(other.EXIF_SIZE_SHORT),
      EXIF_SIZE_SSHORT(other.EXIF_SIZE_SSHORT),
      EXIF_SIZE_LONG(other.EXIF_SIZE_LONG),
      EXIF_SIZE_SLONG(other.EXIF_SIZE_SLONG),
      exif_fields(std::move(other.exif_fields))
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
void exif_reader_t::fmt_exif_byte(const ExifEntry *exif_entry, const char *format, field_value_t& field_value, ExifByteOrder byte_order)
{
   size_t slen;
   char buf[128];

   slen = snprintf(buf, sizeof(buf), format, *exif_entry->data);

   // append additional bytes until we run out of values or buffer is full
   for(size_t i = 1; i < exif_entry->components && slen < sizeof(buf)-1; i++) {
      *(buf+slen++) = ' ';
      slen += snprintf(buf+slen, sizeof(buf)-slen, format, *(exif_entry->data+i));
   }

   // if snprintf indicated truncation, replace last 3 characters with `...` (there's a null character from snprintf)
   if(slen >= sizeof(buf)) {
      slen = sizeof(buf)-4;
      *(buf+slen++) = '.';
      *(buf+slen++) = '.';
      *(buf+slen++) = '.';
   }

   field_value.emplace<std::string>(buf, slen);
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
bool exif_reader_t::fmt_exif_number(const ExifEntry *exif_entry, const char *format, T (*exif_get_type_fn)(const unsigned char*, ExifByteOrder), field_value_t& field_value, size_t item_size, ExifByteOrder byte_order)
{
   size_t slen;
   char buf[128];

   if(exif_entry->components == 1) {
      field_value = exif_get_type_fn(exif_entry->data, byte_order);
      return true;
   }

   slen = snprintf(buf, sizeof(buf), format, exif_get_type_fn(exif_entry->data, byte_order));

   for(size_t i = 1; i < exif_entry->components && slen < sizeof(buf)-1; i++) {
      *(buf+slen++) = ' ';
      slen += snprintf(buf+slen, sizeof(buf)-slen, format, exif_get_type_fn(exif_entry->data+(item_size*i), byte_order));
   }

   // if snprintf indicated truncation, replace last 3 characters with `...` (there's a null character from snprintf)
   if(slen >= sizeof(buf)) {
      slen = sizeof(buf)-4;
      *(buf+slen++) = '.';
      *(buf+slen++) = '.';
      *(buf+slen++) = '.';
   }

   field_value.emplace<std::string>(buf, slen);

   return true;
}

//
// Formats rational numbers within the field value as a set of space-separated
// values.
// 
// Returns false if the formatted string would be truncated and true otherwise.
//
template <typename T>
bool exif_reader_t::fmt_exif_rational(const ExifEntry *exif_entry, T (*exif_get_type_fn)(const unsigned char*, ExifByteOrder), field_value_t& field_value, size_t item_size, ExifByteOrder byte_order)
{
   size_t fsize;
   size_t slen = 0;
   char buf[128];

   for(size_t i = 0; i < exif_entry->components; i++) {
      int64_t numerator, denominator;

      if(i) {
         if(sizeof(buf) - slen == 0)
            return false;

         *(buf+slen++) = ' ';
      }

      T rational = exif_get_type_fn(exif_entry->data + i * item_size, byte_order);

      numerator = static_cast<int64_t>(rational.numerator);
      denominator = static_cast<int64_t>(rational.denominator);

      if(!denominator)
         return false;

      // copied from libexif/0.6.24
      int decimals = (int)(log10(denominator)-0.08+1.0);

      if((fsize = snprintf(buf+slen, sizeof(buf)-slen, "%.*f",
            decimals,
            (double) numerator /
            (double) denominator)) >= sizeof(buf)-slen)
         return false;

      slen += fsize;
   }

   field_value.emplace<std::string>(buf, slen);

   return true;
}

field_bitset_t exif_reader_t::read_file_exif(const std::string& filepath)
{
   field_bitset_t field_bitset;

   ExifData *exif_data = exif_data_new_from_file(filepath.c_str());

   if(!exif_data)
      return field_bitset;

   ExifByteOrder byte_order = exif_data_get_byte_order(exif_data);

   // used only for numeric formatting - strings are copied directly from entries
   char buf[64];

   size_t slen = 0;

   std::optional<size_t> field_index;

   field_bitset.reset();

   for(size_t i = 0; i < EXIF_IFD_COUNT; i++) {
      // ignore EXIF_IFD_1, which is the thumbnail image within the main image
      if(i == static_cast<ExifIfd>(EXIF_IFD_1))
         continue;

      //
      // Walk all tags found in a file and assign a field index based
      // on the tag value. This works out faster than calling libexif
      // for each tag (e.g. exif_content_get_entry) because it loops
      // through tags to find the specified one.
      // 
      // The loop below consists of two sections. The top one formats
      // tags that are less useful if formated in a generic way, such
      // as the f-number or the exposure time values, and the bottom
      // section formats tag values generically, according to their
      // type.
      // 
      // libexif formatting is not used because some of the values may
      // be rendered differently, based on the size of the buffer (e.g.
      // "Cloudy weather" or "Cloudy") and some may contain different
      // representations of the same value (e.g. "2.76 EV (f/2.6)"),
      // which may be confusing in the database. There is also a bit
      // of a performance penalty if exif_entry_get_value is called,
      // because it always memset's the buffer in every call and calls
      // strlen more often than needed.
      // 
      if(exif_data->ifd[i]) {
         for(size_t k = 0; k < exif_data->ifd[i]->count; k++) {
            ExifEntry *exif_entry = exif_data->ifd[i]->entries[k];

            if(!exif_entry)
               continue;

            field_index.reset();
            slen = 0;

            // GPS tags overlap with ExifTag values and cannot be in the same switch
            if(i == static_cast<ExifIfd>(EXIF_IFD_GPS)) {
               switch(exif_entry->tag) {
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
                     //     4  7
                     // YYYY:MM:DD
                     if(exif_entry->format != EXIF_FORMAT_ASCII || exif_entry->size != 11 || exif_entry->data[4] != ':' || exif_entry->data[7] != ':')
                        field_index = EXIF_FIELD_GPSDateStamp;
                     else {
                        std::string& tstamp = exif_fields[EXIF_FIELD_GPSDateStamp].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);
                        tstamp[4] = tstamp[7] = '-';
                        field_bitset.set(EXIF_FIELD_GPSDateStamp);
                     }
                     break;
                  default:
                     break;
               }
            }
            else {
               // EXIF_IFD_0 (main image), EXIF_IFD_EXIF, EXIF_IFD_INTEROPERABILITY
               switch(exif_entry->tag) {
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
                     // YYYY:MM:DD HH:MM:SS
                     if(exif_entry->format != EXIF_FORMAT_ASCII || exif_entry->size != 20 || exif_entry->data[4] != ':' || exif_entry->data[7] != ':')
                        field_index = EXIF_FIELD_DateTime;
                     else {
                        std::string& tstamp = exif_fields[EXIF_FIELD_DateTime].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);
                        tstamp[4] = tstamp[7] = '-';
                        field_bitset.set(EXIF_FIELD_DateTime);
                     }
                     break;
                  case EXIF_TAG_ARTIST:
                     field_index = EXIF_FIELD_Artist;
                     break;
                  case EXIF_TAG_COPYRIGHT:
                     if(exif_entry->format != EXIF_FORMAT_ASCII)
                        field_index = EXIF_FIELD_Copyright;
                     else {
                        //
                        // may be one of 3 forms:
                        // 
                        //   * photographer copyright \x0 editor copyright \x0
                        //   * photographer copyright \x0
                        //   * \x20\x0 editor copyright \x0
                        //
                        if(exif_entry->size > 1) {
                           std::string& copyright = exif_fields[EXIF_FIELD_Copyright].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);

                           // search for the editor copyright separator and replace it with a semicolon
                           const unsigned char *editor = static_cast<const unsigned char*>(memchr(exif_entry->data, '\x0', exif_entry->size-1));
                           if(editor)
                              copyright[editor - exif_entry->data] = ';';

                           field_bitset.set(EXIF_FIELD_Copyright);
                        }
                     }
                     break;
                  case EXIF_TAG_EXPOSURE_TIME:
                     if(exif_entry->format != EXIF_FORMAT_RATIONAL)
                        field_index = EXIF_FIELD_ExposureTime;
                     else {
                        ExifRational rational = exif_get_rational (exif_entry->data, byte_order);

                        if(!rational.denominator)
                           field_index = EXIF_FIELD_ExposureTime;
                        else {
                           double value = (double) rational.numerator / (double) rational.denominator;

                           if(!value || value >= 1)
                              field_index = EXIF_FIELD_ExposureTime;
                           else {
                              slen = snprintf(buf, sizeof(buf), "1/%.0f", 1. / value);

                              exif_fields[EXIF_FIELD_ExposureTime].emplace<std::string>(buf, slen);

                              field_bitset.set(EXIF_FIELD_ExposureTime);
                           }
                        }
                     }
                     break;
                  case EXIF_TAG_FNUMBER:
                     if(exif_entry->format != EXIF_FORMAT_RATIONAL)
                        field_index = EXIF_FIELD_FNumber;
                     else {
                        ExifRational rational = exif_get_rational (exif_entry->data, byte_order);

                        if(!rational.denominator)
                           field_index = EXIF_FIELD_FNumber;
                        else {
                           slen = snprintf(buf, sizeof(buf), "f/%.01f", (double) rational.numerator / (double) rational.denominator);

                           exif_fields[EXIF_FIELD_FNumber].emplace<std::string>(buf, slen);

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
                     // YYYY:MM:DD HH:MM:SS
                     if(exif_entry->format != EXIF_FORMAT_ASCII || exif_entry->size != 20 || exif_entry->data[4] != ':' || exif_entry->data[7] != ':')
                        field_index = EXIF_FIELD_DateTimeOriginal;
                     else {
                        std::string& tstamp = exif_fields[EXIF_FIELD_DateTimeOriginal].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);
                        tstamp[4] = tstamp[7] = '-';
                        field_bitset.set(EXIF_FIELD_DateTimeOriginal);
                     }
                     break;
                  case EXIF_TAG_DATE_TIME_DIGITIZED:
                     if(exif_entry->format != EXIF_FORMAT_ASCII || exif_entry->size != 20 || exif_entry->data[4] != ':' || exif_entry->data[7] != ':')
                        field_index = EXIF_FIELD_DateTimeDigitized;
                     else {
                        std::string& tstamp = exif_fields[EXIF_FIELD_DateTimeDigitized].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);
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
                     if(exif_entry->size > 8 && !memcmp(exif_entry->data, "ASCII\x0\x0\x0", 8)) {
                        // some files identify as ASCII, but store UCS-2, which would put an empty string into the database
                        if(*(exif_entry->data+8) && exif_entry->size > 9 && *(exif_entry->data+9)) {
                           exif_fields[EXIF_FIELD_UserComment].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data+8), exif_entry->size-8);
                           field_bitset.set(EXIF_FIELD_UserComment);
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
                  case EXIF_TAG_DEVICE_SETTING_DESCRIPTION:
                     field_index = EXIF_FIELD_DeviceSettingDescription;
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

            if(field_index.has_value() && exif_entry->size) {
               switch(exif_entry->format) {
                  case EXIF_FORMAT_BYTE:
                     fmt_exif_byte<unsigned char>(exif_entry, "%02hhx", exif_fields[field_index.value()], byte_order);
                     field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_ASCII:
                     // ASCII entries are always null-terminated (some may have null character embedded in the string - e.g. Copyright)
                     if(exif_entry->size > 1) {
                        exif_fields[field_index.value()].emplace<std::string>(reinterpret_cast<const char*>(exif_entry->data), exif_entry->size-1);
                        field_bitset.set(field_index.value());
                     }
                     break;
                  case EXIF_FORMAT_SHORT:
                     if(fmt_exif_number(exif_entry, "%hu", exif_get_short, exif_fields[field_index.value()], EXIF_SIZE_SHORT, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_LONG:
                     if(fmt_exif_number(exif_entry, "%" PRIu32, exif_get_short, exif_fields[field_index.value()], EXIF_SIZE_LONG, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_RATIONAL:
                     if(fmt_exif_rational(exif_entry, exif_get_rational, exif_fields[field_index.value()], EXIF_SIZE_RATIONAL, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_SRATIONAL:
                     if(fmt_exif_rational(exif_entry, exif_get_srational, exif_fields[field_index.value()], EXIF_SIZE_SRATIONAL, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_SBYTE:
                     fmt_exif_byte<char>(exif_entry, "%hhd", exif_fields[field_index.value()], byte_order);
                     field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_UNDEFINED:
                     //
                     // This format type is can be anything, ranging from one byte
                     // (e.g. scene type) to four bytes (e.g. flash PIX version) to
                     // arbitrary structures (e.g. MakerNote). Those fields we know
                     // about should be formatted in the section above and here we
                     // can only output values as hex data, which may be truncated.
                     //
                     fmt_exif_byte<unsigned char>(exif_entry, "%02hhx", exif_fields[field_index.value()], byte_order);
                     field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_SSHORT:
                     if(fmt_exif_number(exif_entry, "%hd", exif_get_sshort, exif_fields[field_index.value()], EXIF_SIZE_SSHORT, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_SLONG:
                     if(fmt_exif_number(exif_entry, "%" PRId32, exif_get_slong, exif_fields[field_index.value()], EXIF_SIZE_SLONG, byte_order))
                        field_bitset.set(field_index.value());
                     break;
                  case EXIF_FORMAT_FLOAT:
                     // unsupported in libexif/0.6.24
                     [[fallthrough]];
                  case EXIF_FORMAT_DOUBLE:
                     // unsupported in libexif/0.6.24
                     [[fallthrough]];
                  default:
                     break;
               }
            }
         }
      }
   }

   exif_data_unref(exif_data);

   return field_bitset;
}

const std::vector<field_value_t>& exif_reader_t::get_exif_fields(void) const
{
   return exif_fields;
}

}
}
