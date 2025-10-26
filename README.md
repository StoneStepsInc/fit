# File Integrity Tracker (fit)

`fit` is a command line application that computes file checksums
for a file tree and can verify file integrity against the same
tree at the original base directory or a new base directory.

`fit` can run in two modes - to scan a file tree and record file
checksums in a SQLite database and to scan the same file tree,
possibly at a different base file path, in order to verify file
checksums against those stored in the SQLite database.

## File Tree Scanning Options

The options to scan a file tree are the same for both scanning
modes and are as follows:

  * `-d c:\path\to\target`

    Points to a directory to scan. If omitted, current directory
    is scanned. May be used multiple times to scan more than one
    directory under the same base path.

  * `-p c:\path\to`

    Specifies the base path that will be removed from file paths
    under the the scanned directory, when stored in the database.

    If omitted, full source path will be recorded in the database
    and only the original file tree can be verified.

    For example, if a user stores their pictures and videos under
    `C:\Users\Alice\Pictures` and `C:\Users\Alice\Videos`, and
    these directories are copied to an external drive, under
    `X:\Alice\`, then a base path `C:\Users` may be used to record
    checksums for pictures and videos and a base path `X:\` may be
    used to verify file copies on the external drive.

  * `-b c:\path\to\database\file`

    Points to a SQLite database file `fit` will use to record and
    verify file checksums.

  * `-r`

    When specified, tells `fit` to scan the directory named via
    `-d` and all of its sub-directories.

  * `-w`

    This option was originally intended to recover from an
    interrupted scan, comparing file modification times instead
    of comparing file hashes, but it became pointless since the
    `-u` option was introduced and it was removed.


  * `-u`

    This option instructs `fit` to update the last scanset instead
    of creating a new one. A typical use of this option will be to
    continue an interrupted scan or process multiple directories
    separately, but keep them within the same scanset.

    When this option is used, all other options must be exactly
    the same as was in the original scan, including their order.

    The scan completion time will be updated after each `-u`
    run and should not be considered as the scan duration for
    repeated scans with the `-u` option.

  * `-i 10`

    Sets the time interval in seconds to report scanning progress.
    If zero is specified, number of processed files is not reported
    during a scan.

  * `-l path\to\log_file.log`

    An optional path to a log file that captures console messages.
    Console messages written to standard output and standard error
    will be prefixed with `inf` and `err`, respectively.

  * `-s file-buffer-size`

    Defines the size of the file read buffer, rounded up to either
    `512` or `4096` bytes. The default value is `524288` bytes.

  * `-a`

    This option instructs `fit` to skip directories with restricted
    access, which by default would interrupt a scan. The default
    behavior makes presence of such directories visible, so it can
    be decided whether to skip them using this option or examine
    reasons why there are such directories in the file tree being
    scanned.

    Note, however, that it may not be possible to report restricted
    directory names, so some other means need to be used to figure
    which specific directories cannot be accessed.

    The `-a` option may be added in subsequent update scans, so
    the inaccessible directory is skipped.

  * `-X .ext[.ext]...`

    This option provides a list of EXIF file extensions. The default
    list is `.jpg.jpeg.cr2.dng.nef.tiff.tif.heif.webp`.
    
    When used without a value, disables EXIF processing altogether.

  * `-J`

    This option instructs `fit` to store EXIF values obtained from
    the Exiv2 library as JSON in the database.

  * `-t 4`

    Number of threads used for hashing and updating file information
    in the database. The default value is `4` threads.

  * `-H 8`

    Maxumum number of multi-buffer hash jobs being performed at the
    same time. Multi-buffer hashing takes advantage of processor
    instructions that apply the same operation against multiple sets
    of different data. The default value is `8` buffers.

    Note that each multi-buffer hash job requires an open file handle,
    so the maximum number of simultaneously opened file handles may
    be exceded for some combinations of `-t` and `-H` options, which
    will be indicated by errors reporting that too many files are
    open.

    This option is not available if the project is built with the
    symbol `NO_SSE_AVX` defined.

  * `-S Windows | POSIX`

    A path separator to be used to query the database when verifying
    files. This option is intended for verifying files locally on a
    different platform. For example, a scan performed on Windows may
    be verified on a Samba server running on Linux via local file
    paths.

    Note that absolute paths cannot be verified this way because
    drive letters or leading path separators will fail to match
    when queried on different platforms.

  * `-v [scan-id]`

    Scans the file tree and reports added, modified or changed files.
    See the `-R` option for reporting removed files.

    If a scan number provided as a value, the file tree will be
    verified against that scan.

  * `-R`

    Instructs `fit` to report removed files against the base scan,
    which is either the last scan or the scan specified in the
    `-v` option. May only be used if `-v` is also specified.

    Note, that for this option to work, the same directories must
    be specified for the verification scan as were used in the base
    scan.

    For example, if the base scan recorded files under `Pictures`
    and `Documents`, and the verification scan only specifies
    `Pictures`, then all files under `Documents` will be reported
    as removed. This happens because file removals are tracked
    against all files recorded in the base scan, not individual
    directories.

  * `-m scan-message`

    Records a short human-readable message for the current scan.
    The message specified in the first scan is stored in the
    database. Scan messages for subsequent update scans may still
    be useful if log files are preserved.

## Scanning a File Tree

Scanning a file tree without the `-v` option will record computed
checksums in the specified SQLite database.

Multiple directories may be specified in a single scan, so they
are recorded in the same scan in the SQLite database. For example,
for a drive that stores pictures and videos in separate root
folders, such as `X:\Pictures` and `X:\Videos`, scanning those
folders as `-d X:\Pictures -d X:\Videos` avoids scanning system
folders `X:\System Volume Information` and `X:\$RECICLE.BIN`, if
`-d X:\` was used.

Alternatively, an existing scan may be updated via `-u`, with
some restrictions, which may be useful for splitting long scans.
See `-u` option for more details.

Note that scanning a file tree multiple times without `-u` or
`-w` option will record multiple independent scans, which may
yield unexpected results during verifiction. For example, if
`X:\Pictures` and `X:\Videos` are scanned independently, in
this order, and then when `X:\` is being verified, all files
from `X:\Pictures` will be reported as new files because they
were not present in the last scan of `X:\Videos`.

## Verifying a File Tree

Scanning a file tree with the `-v` option will compare computed
checksums against those stored in the SQLite database during the
last scan.

Files with mismatching or missing checksums will be reported with
three labels:

  * `new file`

    This file was picked up by a file tree scan, but was not found
    in the database.

  * `modified`

    This file was found in the database and its new checksum does
    not match that of the database record and the current file
    modification time is not equal to the one in the database
    record.

    The current file modification time may be ahead or behind the
    database time if a file was modified or restored from a backup.
    No distinction is made between these two cases.

  * `changed`

    This file was found in the database and its new checksum did
    not match that of the database record, but the file modification
    time is the same as the one in the database record.

    This means that the file changed outside of the usual file
    editing applications, which typically update file modification
    time. For example, disk corruption or direct disk access may
    change file contents without updating file modification time.

With scans performed by `fit` 2.0.0 and newer, it is also possible
to verify which files were changed between scans comparing scan
sets in the database. For example, files changed between scans `11`
and `12` may be listed with this command:

    sqlite3 -line -cmd ".param set @SCAN_ID 12" sqlite.db < sql/list-changed-files.sql

Files with version `1` were added in the specified scan and
contents of files with greater versions were changed.

## Scan Performance

Scan speed in initial `fit` releases mostly depended on the
number of threads and the hash buffer size, so it was easier
to estimate scan performance in different configurations.
However, as more features were added, it became harder to
predict scan performance based on configuration parameters.

This section describes various configuration parameters
that affect scan performance and may be useful for fine-tuning
scan speed before large scans. In general, it might be a good
idea to run a test scan against a sample directory using a
throw-away database.

Multiple files are scanned in parallel using `-t` threads.
Each thread has its own EXIF reader, a file hasher and a
set of hashing buffers.

Using more threads increases parallelism, but also increases
contention for shared resources, such as disk and database.
In general, 1-2 threads will work better for magnetic drives
and 8-16 threads will work better for solid state drives.

Keeping the SQLite database on a different disk from the one
being scanned should be the default approach because otherwise
scan performance will visibly deteriorate.

Each scan thread maintains its own set of hashing buffers,
so each scan thread will open `-H` files, will read `-s`
bytes from each file, and will hash this amount in parallel,
reading more data, `-s` bytes at a time, as hashing progresses.
This means that only hashing is done in parallel on a single
scan thread, while files are being read one at a time, which
may improve scan speed against drives that provide slower
random disk access, such as magnetic drives.

Increasing buffer size via larger `-s` values may help to
improve scan speed against large files, such as video and
image files in RAW format, which may be stored sequentially
on disk, but may also create more disk activity for fragmented
files because of the increased disk seeking.

Antivirus software can significantly slow down scans if
the target directory contains many executables or libraries
because file open operations are typically intercepted for
these types of files. The difference may be as much as
scanning at 19 MB/s with the antivirus being active vs.
75 MB/s with the target directory temporarily added to the
exclusion list for the duration of the scan. Don't forget
to remove the exclusion after the scan.

## SQLite Database

The SQLite database contains tables described in this section.

All text fields are stored as UTF-8 characters. Note that all
text comparisons in SQLite are case-sensitive and `ABC` will not
compare equal to `abc`. Moreover, if case-insensitive collation
is used in queries, it will only work with ASCII characters and
will not apply across all Unicode characters.

### Scans Table

The `scans` table contains a record for each run of `fit`
without the `-v` option and has the following fields:

  * `id` `INTEGER NOT NULL PRIMARY KEY`

    SQLite maintains this column automatically by aliasing
    `rowid`.

  * `app_version` `TEXT NOT NULL`

    Version of the application that generated this scan record.

  * `scan_time` `INTEGER NOT NULL`
    
    Number of seconds since 1970-01-01, UTC of the time when the
    scan was started. Use this expression to output it as a calendar
    time in SQLite shell.

        datetime(scan_time, 'unixepoch')

  * `completed_time` `INTEGER`

    Number of seconds since 1970-01-01, UTC of the time the
    scan was completed. May be updated by subsequent scans
    with the `-u` option.

  * `last_update_time` `INTEGER`

    Number of seconds since 1970-01-01, UTC of the time when
    the last scan update was started (i.e. the `-u` option was
    used).

    A scan is considered completed if `completed_time` contains
    a non-`NULL` value and `last_update_time` either contains
    `NULL` or a time stamp that is in the past, relative to
    `completed_time`.

  * `cumulative_duration` `INTEGER`

    Cumulative time, in seconds, spent while scanning files.
    This value is updated for original scans and update scans,
    never for verification scans.

  * `times_updated` `INTEGER`

    Number of times the last scan was updated. This number does
    not include the original scan.

  * `base_path` `TEXT`

    The base path, derived from the `-p` option.

  * `options` `TEXT NOT NULL`

    Command line options used for this scan. The value will not
    include options that can be changed between update scans,
    such as `-d` or `-a`.

  * `message` `TEXT`

    A text message to describe this scan. Only the scan message
    of the first scan is stored in the database.

### Versions Table

The `versions` table contains a record per scanned file that has
a different hash value from the previous version of the same file.
This table has following fields:

  * `id` `INTEGER NOT NULL PRIMARY KEY`

    A version record identifier aliasing `rowid`.

  * `file_id` `INTEGER NOT NULL`

    A file identifier for this version record. Multiple versions of
    the same file path have the same `file_id` value.

  * `version` `INTEGER NOT NULL`

    File record version. Starts with `1` and is incremented every
    time a new hash value is computed for this file path. Previous
    file version records are kept intact.

    The last version is always used when comparing file checksums
    during scans.

  * `mod_time` `INTEGER  NOT NULL`

    File modification time, in seconds since 1970-01-01, UTC. Use
    this expression to output it as a calendar time in SQLite shell.

        datetime(scan_time, 'unixepoch')

  * `entry_size` `INTEGER NOT NULL`

    A file size, in bytes, as reported by a directory entry for this
    file.

  * `read_size` `INTEGER NOT NULL`

    A file size, in bytes, as computed while reading the file
    until there is no more data. In most cases this value will
    be exactly as stored in `entry_size`, unless either the
    file or the directory entry was updated after it was read
    by the file scanner.

  * `exif_id` `INTEGER`

    An EXIF record identifier for this file version record. This
    value is set to `NULL` for file versions that do not have
    EXIF data associated with them.

  * `hash_type` `VARCHAR(32) NOT NULL`

    File checksums are computed as a SHA-256 hash in the current
    version of the application, so this column will always be set
    to `SHA256`.

  * `hash` `TEXT`

    A file checksum value in hex format using lowercase characters
    for letters `abcdef`. A hash will be `NULL` for zero-length
    files.

### Files Table

The `files` table contains a record per file path. Multiple versions
of the same file reference the same file record.

  * `id` `INTEGER NOT NULL PRIMARY KEY`

    A file record identifier aliasing `rowid`.

  * `name` `TEXT NOT NULL`

    File name without file path. This field is only useful for file
    name queries and it will contain numerous duplicates across
    multiple file path versions and files with the same name located
    in different directories.

    This field is not indexed and a full table scan will be performed
    for every query that uses the file name as the only criteria. It
    is useful for file name queries to avoid a `LIKE` clause against
    the file path that compares just the file name at the end of the
    path.

  * `ext` `TEXT`

    File extension, including the leading dot, as reported by the
    underlying file system layer.

  * `path` `TEXT NOT NULL`

    A file path with the base path removed, if a base path is used.

    File paths are versioned and the latest version should be selected
    to obtain the record for the most recent scan.
   

### Scansets Table

The `scansets` table contains a record per scanned file, whether
there is a new version of the file detected or not.

  * `id` `INTEGER NOT NULL PRIMARY KEY`

    A scan set record identifier aliasing `rowid`.

  * `scan_id` `INTEGER NOT NULL`

    A scan record identifier.

  * `version_id` `INTEGER NOT NULL`

    A file version record identifier.

This table represents the set of files scanned in a single `fit`
run.

### EXIF Table

Files with extensions in the list below are also scanned for EXIF
information.

    .jpg .jpeg .png .cr2 .dng .nef .tiff .tif .heif .webp

If EXIF data is found in the file being scanned, EXIF values listed
below are recorded in the `exif` table.

  * BitsPerSample, Compression, DocumentName, ImageDescription, 
  * Make, Model, Orientation, SamplesPerPixel, 
  * Software, DateTime, Artist, Copyright, 
  * ExposureTime, FNumber, ExposureProgram, ISOSpeedRatings, 
  * SensitivityType, ISOSpeed, TimeZoneOffset, DateTimeOriginal,
  * DateTimeDigitized, OffsetTime, OffsetTimeOriginal, OffsetTimeDigitized,
  * ShutterSpeedValue, ApertureValue, SubjectDistance, BrightnessValue,
  * ExposureBiasValue, MaxApertureValue, MeteringMode, LightSource,
  * Flash, FocalLength, UserComment, SubsecTime,
  * SubSecTimeOriginal, SubSecTimeDigitized, FlashpixVersion, FlashEnergy,
  * SubjectLocation, ExposureIndex, SensingMethod, SceneType,
  * ExposureMode, WhiteBalance, DigitalZoomRatio, FocalLengthIn35mmFilm,
  * SceneCaptureType, DeviceSettingDescription, SubjectDistanceRange, ImageUniqueID,
  * CameraOwnerName, BodySerialNumber, LensSpecification, LensMake,
  * LensModel, LensSerialNumber, GPSLatitudeRef, GPSLatitude,
  * GPSLongitudeRef, GPSLongitude, GPSAltitudeRef, GPSAltitude,
  * GPSTimeStamp, GPSSpeedRef, GPSSpeed, GPSDateStamp
  * XMP.xmp.Rating

See this page for EXIF tag descriptions:

https://exiv2.org/tags.html

Most EXIF values are recorded as-is, without translating them into
human-readable formats. For example, `ExposureProgram` is recorded
as an integer, not as `Manual`, `Aperture priority`, etc.

Single numeric values are stored as integers and decimal values
are stored as strings or string lists. For example, `GPSLongitude`
is recorded in EXIF as 3 decimal values, which are stored in the
`exif` table as text similar to `79 36 4.1143`. `ApertureValue`,
on the other hand, is recorded as a decimal string `2.97`, which
can be used to compute FNumber as `2 ^ (2.97/2) = f/2.8`.

EXIF values are experimental at this point and their format may
change in the future.

### Exiv2Json

If `-J` option was used, additional EXIF values obtained from
the Exiv2 library are stored as JSON in the `Exiv2Json` column
of the `exif` table.

Note that using `-J` option will significantly increase the
size of the database. For example, a database containing scans
of 207,208 files, 186,515 of which are photos with EXIF, will
be approximately 125 MB in size. The same number of files
scanned with the `-J` option will produce a database that is
approximately 1,067 MB in size.

JSON values in this column may be different from text values
described on the Exiv2 page above.

For example, `Exif.GPSInfo.GPSLongitude` is described as a
sequence of 3 rational values formatted as `ddd/1,mm/1,ss/1`,
by Exiv2. This value is stored in the `exif.GPSLongitude`
column as a text value similar to `53 23 6.387` and translates
into the longitude value of `53°23'06.4"`. The same value is
stored in JSON as a sequence of numerator/denominator pairs,
similar to this:

    [[53,1],[23,1],[6387,1000]]

Inividual JSON values may be obtained from the `Exiv2Json` column
using JSON functions in SQLite, which are described on this page.

https://www.sqlite.org/json1.html

For example, in order to obtain camera make and model, following
JSON functions can be used in the SQL selection list.

    json_extract(Exiv2Json, '$.Exif.Image.Make'),
    json_extract(Exiv2Json, '$.Exif.Image.Model')

Names of the JSON fields are obtained from Exiv2 and will not
correspond to field names obtained from different tools. For
example, `exiftool` may show `TimeZone` in `MakeNotes` group
or under `MakerNoteCanon/TimeInfo/TimeZone` in verbose mode,
while Exiv2 will report it as `Exif.CanonTi.TimeZone`.

Exiv2 website is a good source of information about JSON schema,
but a quick exploratory way to list keys in the `Exiv2Json`
column for some file name is to run the `list-exiv2json-fields.sql`
script, as shown below.

    sqlite3 -box -cmd ".param set @FILEPATH _MG_2280.CR2" \
        sqlite.db < sql\list-exiv2json-fields.sql

Values from the `Exiv2Json` column can be used in SQL just
like any other values. For example, in order to obtain count
of images grouped by lens model recorded in Canon maker notes,
this SQL can be used.

    SELECT
        count(*), json_extract(Exiv2Json, '$.Exif.Canon.LensModel')
    FROM exif
        JOIN versions ON exif_id = exif.rowid
        JOIN files ON file_id = files.rowid
        JOIN scansets ON version_id = versions.rowid
    WHERE scan_id = 2
    GROUP by json_extract(Exiv2Json, '$.Exif.Canon.LensModel')
    ORDER by 1 DESC;

EXIF entries are limited to 12 elements in `Exiv2Json` to keep
the size of this column manageable. Fields with more than 12 
elements are discarded, which typically would affect entries
such as `Exif.Canon.DustRemovalData`. Names of discarded entries
are captured in the `$._fit.oversized` array.

Most of the values in `Exiv2Json` will be obtained from Exiv2
and may be different from the same values in the corresponding
columns of the `exif` table. For example, `$.Exif.Photo.DateTimeOriginal`
will contain an actual EXIF value, such as `2017:01:01 15:23:49`,
compared to `2017-01-01 15:23:49` stored in the `DateTimeOriginal`
column.

EXIF fields that are supposed to contain ASCII values are validated
as UTF-8 fields, which includes ASCII. Fields containing invalid
UTF-8 sequences are discarded and their names are captured in the
`$._fit.bad_utf8` array.

### Useful SQL

You can run SQL queries against the SQLite database using the
SQLite shell. A few SQL scripts can be found in the `sql` directory.
Those SQL scripts that do not require input can be executed as
follows:

    sqlite3 -line sqlite.db < sql/list-scans.sql

The `-line` switch lists each column on its own line. SQlite has
a few more output options, such as `-json`, `-csv` or `-box`.

Scripts that require input may be executed as follows:

    sqlite -line -cmd ".param set @FILENAME abc.txt" < sql/show-file-by-name.sql

See each script for available input values.

### Upgrading Database

The database may occasionally be changed between application
releases and needs to be upgraded before the new version of the
application can work with the database file.

Database upgrades are not automatic and need to be performed
manually via SQL script with matching from/to database schema
versions in the `sql` directory. The source and target database
versions are reported by `fit` when the database cannot be
opened because of a database version mismatch.

Database versions must be updgraded sequentially, from the
oldest version and until the desired version is reached. Use
this command with the appropriate script to upgrade a database.

    sqlite3 sqlite.db < upgrade-db_1.0-2.0.sql

Note that the database version is distinctly different from
the application version and is changed only when the database
schema is modified.

Some of the syntax used in upgrade scripts may be incompatible
with older versions of SQLite. For example, prior to version
3.35.0 SQLite did not implement `DROP COLUMN`. Databases may
be upgraded on different systems using a newer SQLite version
in this case.

### Upgrading Database v5.0 to v6.0

File version records in the database schema prior to v6.0
were maintained in native file system clock units, which
were different between operating systems.

Starting from the database schema v6.0, which was released
in `fit` v3.0.0, file version time stamps are maintained
in seconds since 1970-01-01, UTC, which requires databases
created by prior versions of `fit` to be upgraded, as
described in this section.

File version time stamps cannot be updated via a single SQL
statement because each time stamp must be adjusted according
to the time zone settings. A special upgrade mode option is
available in `fit` to perform this upgrade.

Note that due to potential number of records that must be
updated, SQL transactions are not used for this update. Make
sure to create a copy of the database file before running
this upgrade, in case if the upgrade fails. If any errors
are reported after the upgrade has started, the database
will become unusable because it will be hard to distinguish
updated time stamps from the original ones.

Run this command to upgrade the database schema v5.0 to v6.0.
Make sure to run this command on the same operating system as
was used for original scans.

    fit --upgrade-schema=6.0 -b c:\path\to\database\file

This upgrade may take a long time, depending on the size of
the database (e.g. on an average computer, it takes about 6
minutes to update 100,000 records).

`fit` will output a `.` for each 1000 records updated and
will wrap each dotted line after 50,000 records to show
progress.

Interrupting the upgrade process will render database unusable.

### A note on `id` vs. `rowid`

Prior to v3.2.0, `fit` used `rowid` for all joins between
entity tables. Given that `fit` never deletes records, using
`rowid` didn't have any side effects.

That is, if some records are deleted and then `VACUUM` is
used, `rowid` values may be reassigned to fill gaps in
`rowid` sequences, which would likely break the referential
integrity of the database and would make it unusable.

Starting from `fit` v3.2.0, `id` columns are added to alias
`rowid` columns, which instructs SQLite to respect `rowid`
values.

Both, `rowid` and `id` can be used interchangeably, so all
existing SQL scripts will still work, without any changes.

Note, however, that existing databases cannot be updgraded
to introduce the new primary key column because SQLite
does not allow adding primary keys to existing tables. The
existing schema will work as before, as long as no records
are being deleted.

## Source

### Windows

Current source requires Visual Studio 2022 to build. The project
is set up to use Nuget packages for all dependencies.

### Linux

Current source compiles on Linux, but very little testing is
done to verify the results.

For a list of packages required to build the project on some
of the Linux flavors, see Docker files in the `devops` directory.

Dependencies that are not available as development packages can
be obtained with `get-*` scripts from the `devops` directory,
such as `devops/get-isa-l_crypto`.

## License

This application is licensed under BSD-3 terms. Read the `LICENSE`
file in the application package for details.

### 3rd-Party Code

This application uses following 3rd-party libraries, licensed separately.

#### SQLite

A SQL database management library.

LICENSE: [Public Domain](https://www.sqlite.org/copyright.html)

#### Exiv2

A library for parsing EXIF data.

LICENSE: [GPL-2.0](https://github.com/Exiv2/exiv2/blob/0.27-maintenance/COPYING)


#### SHA256

An implementation of the SHA-256 secure hash algorithm

LICENSE: [Public Domain](https://github.com/jb55/sha256.c/blob/master/LICENSE)

#### RapidJSON

A library to generate JSON.

LICENSE: [MIT](https://github.com/Tencent/rapidjson/blob/master/license.txt)

#### isa-l_crypto

Intel's (R) Intelligent Storage Acceleration Library Crypto Version.

LICENSE: [BSD 3-Clause License](https://github.com/intel/isa-l_crypto/blob/master/LICENSE)

#### libfmt

A C++ text formatting library.

LICENSE: https://github.com/fmtlib/fmt/blob/10.x/LICENSE
