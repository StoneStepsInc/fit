#ifndef FIT_EXIF_READER_H
#define FIT_EXIF_READER_H

#include <libexif/exif-tag.h>
#include <libexif/exif-data.h>

#include <vector>
#include <variant>
#include <string>
#include <bitset>

#include <cstddef>

namespace fit {
namespace exif {

enum field_index_t {
   EXIF_FIELD_BitsPerSample,
   EXIF_FIELD_Compression,
   EXIF_FIELD_DocumentName,
   EXIF_FIELD_ImageDescription,
   EXIF_FIELD_Make,
   EXIF_FIELD_Model,
   EXIF_FIELD_Orientation,
   EXIF_FIELD_SamplesPerPixel,
   EXIF_FIELD_Software,
   EXIF_FIELD_DateTime,
   EXIF_FIELD_Artist,
   EXIF_FIELD_Copyright,
   EXIF_FIELD_ExposureTime,
   EXIF_FIELD_FNumber,
   EXIF_FIELD_ExposureProgram,
   EXIF_FIELD_ISOSpeedRatings,
   EXIF_FIELD_TimeZoneOffset,
   EXIF_FIELD_SensitivityType,
   EXIF_FIELD_ISOSpeed,
   EXIF_FIELD_DateTimeOriginal,
   EXIF_FIELD_DateTimeDigitized,
   EXIF_FIELD_OffsetTime,
   EXIF_FIELD_OffsetTimeOriginal,
   EXIF_FIELD_OffsetTimeDigitized,
   EXIF_FIELD_ShutterSpeedValue,
   EXIF_FIELD_ApertureValue,
   EXIF_FIELD_SubjectDistance,
   EXIF_FIELD_BrightnessValue,
   EXIF_FIELD_ExposureBiasValue,
   EXIF_FIELD_MaxApertureValue,
   EXIF_FIELD_MeteringMode,
   EXIF_FIELD_LightSource,
   EXIF_FIELD_Flash,
   EXIF_FIELD_FocalLength,
   EXIF_FIELD_UserComment,
   EXIF_FIELD_SubsecTime,
   EXIF_FIELD_SubSecTimeOriginal,
   EXIF_FIELD_SubSecTimeDigitized,
   EXIF_FIELD_FlashpixVersion,
   EXIF_FIELD_FlashEnergy,
   EXIF_FIELD_SubjectLocation,
   EXIF_FIELD_ExposureIndex,
   EXIF_FIELD_SensingMethod,
   EXIF_FIELD_SceneType,
   EXIF_FIELD_ExposureMode,
   EXIF_FIELD_WhiteBalance,
   EXIF_FIELD_DigitalZoomRatio,
   EXIF_FIELD_FocalLengthIn35mmFilm,
   EXIF_FIELD_SceneCaptureType,
   EXIF_FIELD_SubjectDistanceRange,
   EXIF_FIELD_ImageUniqueID,
   EXIF_FIELD_CameraOwnerName,
   EXIF_FIELD_BodySerialNumber,
   EXIF_FIELD_LensSpecification,
   EXIF_FIELD_LensMake,
   EXIF_FIELD_LensModel,
   EXIF_FIELD_LensSerialNumber,
   EXIF_FIELD_GPSLatitudeRef,
   EXIF_FIELD_GPSLatitude,
   EXIF_FIELD_GPSLongitudeRef,
   EXIF_FIELD_GPSLongitude,
   EXIF_FIELD_GPSAltitudeRef,
   EXIF_FIELD_GPSAltitude,
   EXIF_FIELD_GPSTimeStamp,
   EXIF_FIELD_GPSSpeedRef,
   EXIF_FIELD_GPSSpeed,
   EXIF_FIELD_GPSDateStamp,
   EXIF_FIELD_FieldCount
};

typedef std::variant<nullptr_t, int64_t, std::string> field_value_t;

typedef std::bitset<EXIF_FIELD_FieldCount> field_bitset_t;

//
// A class that reads EXIF data from a file and maps EXIF values
// to database columns via a field index.
//
class exif_reader_t {
   private:
      size_t EXIF_SIZE_RATIONAL;
      size_t EXIF_SIZE_SRATIONAL;
      size_t EXIF_SIZE_SHORT;
      size_t EXIF_SIZE_SSHORT;
      size_t EXIF_SIZE_LONG;
      size_t EXIF_SIZE_SLONG;

      std::vector<field_value_t> exif_fields;

   private:
      template <typename T>
      static void fmt_exif_byte(const ExifEntry *exif_entry, const char *format, field_value_t& field_value, ExifByteOrder byte_order);

      template <typename T>
      static bool fmt_exif_number(const ExifEntry *exif_entry, const char *format, T (*exif_get_type_fn)(const unsigned char*, ExifByteOrder), field_value_t& field_value, size_t item_size, ExifByteOrder byte_order);

      template <typename T>
      static bool fmt_exif_rational(const ExifEntry *exif_entry, T (*exif_get_type_fn)(const unsigned char*, ExifByteOrder), field_value_t& field_value, size_t item_size, ExifByteOrder byte_order);

   public:
      exif_reader_t(void);

      exif_reader_t(exif_reader_t&& other);

      field_bitset_t read_file_exif(const std::string& filepath);

      const std::vector<field_value_t>& get_exif_fields(void) const;
};

}
}

#endif // FIT_EXIF_READER_H