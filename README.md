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

    This option instructs `fit` to compare the file modification
    time against the time stored in the SQLite database for each
    file as an indicator whether the file was changed or not,
    rather than to compute checksums. This option was originally
    intended to recover from an interrupted scan, but using the
    `-u` option works better for this.

  * `-u`

    This option instructs `fit` to update the last scanset instead
    of creating a new one. A typical use of this option will be to
    continue an interrupted scan or process multiple directories
    separately, but keep them within the same scanset.

    When this option is used, all other options must be exactly
    the same as was in the original scan, including their order.

    Note that the last scan record is not updated when this option
    is used and all additionally scanned directories will appear
    as if they were selected and scanned in the initial run.

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
    `512` or `4096` bytes. The default buffer size is `65536` bytes.

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

  * `-X`

    This option provides a list of EXIF file extensions. The default
    list is `.jpg.jpeg.cr2.dng.nef.tiff.tif.heif.webp`.
    
    When used without a value, disables EXIF processing altogether.

  * `-J`

    This option instructs `fit` to store EXIF values obtained from
    the Exiv2 library as JSON in the database.

## Scanning a File Tree

Scanning a file tree without the `-v` option will record computed
checksums in the specified SQLite database.

Multiple directories may be recorded in the same SQLite database
via multiple scans. For example, for a drive that stores pictures
and videos in separate root folders, such as `X:\Pictures` and
`X:\Videos`, scanning those folders individually avoids scanning
system folders `System Volume Information` and `$RECICLE.BIN`,
if `-d X:\` was used.

Each file tree scan is recorded separately in the SQLite database
and may be accompanied by a text message using the `-m` option,
which may be useful when reviewing scan records later.

## Verifying a File Tree

Scanning a file tree with the `-v` option will compare computed
checksums against those stored in the SQLite database during the
scan phase.

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

Scan speed will depend on the type of disks being scanned and
may be controlled to some degree with the `-t` option, which
specifies how many threads should be used for scanning.

However, as more `fit` features are added, the database schema
is becoming more complex, which requires more elaborate database
operations and results is slower performance, regardless of the
number of threads used. The numbers below show typical scan
speed for SSD and magnetic drives in relation to the number of
threads.

For disks that allow fast random access, such as SSD drives,
more threads, to a limit, will result in better performance.
For example, scanning an internal SSD drive yields these scan
times, depending on the number of threads used.

  * `-t 2` scans at `51.1` MB/sec
  * `-t 4` scans at `58.6` MB/sec
  * `-t 16` scans at `65.9` MB/sec
  * `-t 32` scans at `62.8` MB/sec

For magnetic disks, more threads reading the disk results in
more contention and using fewer threads results in better scan
speed. The numbers below are captured against a 4-drive Storage
Space attached via USB 3.0 connected to a Thunderbolt docking
station.

  * `-t 1` scans at `30.1` MB/sec
  * `-t 2` scans at `33.7` MB/sec
  * `-t 4` scans at `35.9` MB/sec
  * `-t 16` scans at `35.1` MB/sec
  * `-t 32` scans at `33.9` MB/sec

When scanning magnetic drives, keeping the SQLite database on
a different drive will improve scanning speed.

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

  * `rowid` `INTEGER NOT NULL`

    SQLite maintains this column automatically and it is not
    explicitly included in the schema. It has to be included
    explicitly in the `select` statement to be visible in
    results. For example:

        select rowid, * from scans;

  * `app_version` `TEXT NOT NULL`

    Version of the application that generated this scan record.

  * `scan_time` `INTEGER NOT NULL`
    
    Number of seconds since 1970-01-01, UTC. Use this expression
    to output it as a calendar time in SQLite shell.

        datetime(scan_time, 'unixepoch')

  * `base_path` `TEXT`

    The base path, derived from the `-p` option.

  * `options` `TEXT NOT NULL`

    Command line options used for this scan.

  * `message` `TEXT`

    A text message to describe this scan. If omitted, `NULL` is stored.

### Versions Table

The `versions` table contains a record per scanned file that has
a different hash value from the previous version of the same file.
This table has following fields:

  * `rowid` `INTEGER NOT NULL`

    A version record identifier.

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

    File modification time in platform-specific units. It should not
    be assumed to be a Unix epoch time stamp. For example, on Windows
    this value contains the number of seconds since 1601-01-01, UTC
    and may be converted to Unix epoch by subtracting the value for
    1970-01-01, UTC from the file time, which translates into this
    SQLite clause:

        datetime(mod_time-11644473600, 'unixepoch')

  * `entry_size` `INTEGER NOT NULL`

    A file size, in bytes, as reported by a directory entry for this
    file.

  * `read_size` `INTEGER NOT NULL`

    A file size, in bytes, as computed while reading the file
    until there is no more data. In most cases this value will
    be exactly as stored in `entry_size`, unless either the
    file or the directory entry was updated after it was read
    by the file scanner.

  * `exif_id` `INTEGER NULL`

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

  * `rowid` `INTEGER NOT NULL`

    A file record identifier.

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

  * `ext` `TEXT NULL`

    File extension, including the leading dot, as reported by the
    underlying file system layer.

  * `path` `TEXT NOT NULL`

    A file path with the base path removed, if a base path is used.

    File paths are versioned and the latest version should be selected
    to obtain the record for the most recent scan.
   

### Scansets Table

The `scansets` table contains a record per scanned file, whether
there is a new version of the file detected or not.

  * `rowid` `INTEGER NOT NULL`

    A scan set record identifier.

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

JSON values in this table may be different from text values
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
column is to use `json_each()` function. For example, this
SQL will return names of all keys under the JSON root, which
is identified as the `$` character, for the image identified
by name as `_MG_2280.CR2`.

    SELECT x.key
    FROM json_each(Exiv2Json, '$') AS x,
        exif JOIN versions ON exif_id = exif.rowid
            JOIN files ON file_id = files.rowid
        WHERE name = '_MG_2280.CR2';

This SQL will typically return rows `Exif`, `_fit` and `Xmp`.
Adding field names after `$` will list fields for that level,
such as `$.Exif` may list fields `Image`, `Photo`, `CanonCs`,
`Canon`, `CanonSi`, `CanonTi` and a few others.

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
are captured in the `$._fit.DroppedFields` array.

Most of the values in `Exiv2Json` will be obtained from Exiv2
and may be different from the same values in the corresponding
columns of the `exif` table. For example, `$.Exif.Photo.DateTimeOriginal`
will contain an actual EXIF value, such as `2017:01:01 15:23:49`,
compared to `2017-01-01 15:23:49` stored in the `DateTimeOriginal`
column. 

### Useful SQL

You can run SQL queries against the SQLite database using the
SQLite shell. A few SQL scripts can be found in the `sql` directory.
Those SQL scripts that do not require input can be executed as
follows:

    sqlite3 -line sqlite.db < sql/list-scans.sql

The `-line` switch lists each column on its own line. SQlite has
a few more output options, such as `-json`.

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

## Source

### Windows

Current source requires Visual Studio 2019 to build. It uses
Nuget package for database access and reading EXIF and a 3rd-party
source package to compute SHA-256 hashes.

SHA-256 does not have a package and may be obtained via a batch
file included in the project (`get-sha256.bat`). After running
this batch file, a directory `src/sha256` will contain the source
for the SHA-256 library.

### Linux

Current source compiles on Linux, but very little testing was
done to verify the results.

SQLite development package needs to be installed (e.g. `sqlite-devel`
on Fedora). See Docker files in `devops` for a list of packages
required to build on various Linux flavors.

SHA-256 does not have a package and may be obtained via a script
included in the project (`get-sha256`). After running the script,
a directory `src/sha256` will contain the source for the SHA-256
library.

Linux time stamps will appear as negative values in the SQLite
database and it is not clear at this point how they are computed.

Time stamps can be shown around the actual file modification
time by adding `6437646000`, but it appears that this does not
include some of time zone adjustments.

## License

This application is licensed under BSD-3 terms. Read the `LICENSE`
file in the application package for details.

### 3rd-Party Code

This application uses following 3rd-party libraries, licensed separately.

#### SQLite

A SQL database management library.

LICENSE: [Pubic Domain](https://www.sqlite.org/copyright.html)

#### Exiv2

A library for parsing EXIF data.

LICENSE: [GPL-2.0](https://github.com/Exiv2/exiv2/blob/0.27-maintenance/COPYING)


#### SHA256

An implementation of the SHA-256 secure hash algorithm

LICENSE: [Public Domain](https://github.com/jb55/sha256.c/blob/master/LICENSE)
