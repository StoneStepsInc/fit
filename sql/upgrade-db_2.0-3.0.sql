--
-- This script upgrades the database schema from version 2.0 to
-- version 3.0.
--
-- sqlite3 sqlite.db < upgrade-db_2.0-3.0.sql
--
-- Version literals are not used because .param does not work in
-- PRAGMA. Search for VER_FROM and VER_TO comments to identify
-- where versions must be updated.
--

-- stop and exit if any statement triggered an error
.bail on

BEGIN TRANSACTION;

.print Checking current database version

--
-- Make sure the current database version is what we expect.
--
-- SQLite does not have conditional script statements, so
-- we trigger a SQL constraint error instead when the user
-- version doesn't match the expected value.
--
CREATE TEMPORARY TABLE user_version_trap (
  never_null INTEGER NOT NULL
);

-- the value in WHEN is the expected version
INSERT INTO user_version_trap VALUES (
  CASE (select user_version from pragma_user_version())
    WHEN 20 THEN 1                  -- VER_FROM
    ELSE NULL
  END
);

.print Upgrading database to version 3.0

CREATE TABLE IF NOT EXISTS upgrades (
  upgrade_from INTEGER NOT NULL PRIMARY KEY,
  upgrade_to INTEGER NOT NULL,
  upgrade_time INTEGER NOT NULL
);

--
-- unixepoch() was added in SQLite 3.38.0 - use strftime() instead
--
INSERT INTO upgrades (
  upgrade_from,
  upgrade_to,
  upgrade_time
) VALUES (
  (select user_version from pragma_user_version()),
  30,                               -- VER_TO
  CAST(strftime('%s', 'now') AS INTEGER)
);

--
-- Add a new table for scansets, which contains file versions encountered in a scan
--
CREATE TABLE scansets (scan_id INTEGER NOT NULL, version_id INTEGER NOT NULL);

CREATE UNIQUE INDEX ix_scansets_scan_file ON scansets (scan_id, version_id);

--
-- Collapse all existing scans into one scan set. There is not much
-- else can be done with limited scripting capabilities in the current
-- SQLite shell (i.e. we would need a way to loop over scans and combine
-- file versions for unchanged and new file versions for each adjacent
-- pair of scans). There is also a limitation of the existing schema in
-- that deleted files cannot be distinguished from unchanged files
-- because in neither case there will be a new version.
--
INSERT INTO scansets (
    scan_id,
    version_id
) SELECT
        (select MAX(rowid) FROM scans),
        MAX(rowid) as version_id
    FROM files
    GROUP BY path;

--
-- Preserve file versions per scan, for reference. This table is
-- not used in the application and can be dropped, if not needed.
-- It should be noted that the scan_id column is dropped further
-- in this script and this table can be used to figure out when
-- some file version was added or changed. This table is different
-- from scansets in that it only contains changed file versions
-- per scan, while scansets contains all file versions encountered
-- in a scan.
--
CREATE TABLE _historic_scan_versions (scan_id INTEGER NOT NULL, version_id INTEGER NOT NULL);

INSERT INTO _historic_scan_versions (
    scan_id,
    version_id
) SELECT
        scan_id,
        MAX(rowid) as version_id
    FROM files
    GROUP BY scan_id, path;

-- there's no way we can recreate options, so track them with an invalid option character
ALTER TABLE scans ADD COLUMN options TEXT NOT NULL DEFAULT '-';

-- DROP COLUMN is not available prior to SQLite/3.35.0
ALTER TABLE scans DROP COLUMN current_path;
ALTER TABLE scans DROP COLUMN scan_path;

--
-- Rename the files table to versions and create a new files
-- table for file information unrelated to scans.
--

DROP INDEX ix_files_path;
DROP INDEX ix_files_hash;

ALTER TABLE files RENAME TO versions;

ALTER TABLE versions ADD COLUMN file_id INTEGER NOT NULL DEFAULT 0;

-- DROP COLUMN is not available prior to SQLite/3.35.0
ALTER TABLE versions DROP COLUMN scan_id;

CREATE TABLE files (
    name TEXT NOT NULL,
    ext TEXT NULL,
    path TEXT NOT NULL
);

-- add a temporary non-unique index for the following inserts/updates
CREATE INDEX _temp_ix_files_path ON files (path);

--
-- Populate the files table from the existing file data in the
-- versions table.
--
-- Extensions may be NULL for older scans and we may end up with
-- duplicate paths if same path has an extension in a newer scan.
-- Allow this to happen and then explicitly delete duplicate paths
-- that have NULL extensions, but leaving original paths that do
-- not have extensions alone (e.g. `.gitignore`).
--
INSERT INTO files (name, ext, path) SELECT DISTINCT name, ext, path FROM versions;

-- delete duplicate paths with a NULL extension and leave actual NULL extensions alone
DELETE FROM files WHERE ext IS NULL AND path IN (SELECT path FROM files GROUP BY path HAVING COUNT(path) > 1);

-- update version records to reference new file records
UPDATE versions SET file_id = (SELECT files.rowid FROM files WHERE files.path = versions.path);

-- done with inserts/updates and can drop the temporary index
DROP INDEX _temp_ix_files_path;

-- attempt to create a unique path index before we drop those columns
CREATE UNIQUE INDEX ix_files_path ON files (path);

-- DROP COLUMN is not available prior to SQLite/3.35.0
ALTER TABLE versions DROP COLUMN name;
ALTER TABLE versions DROP COLUMN ext;
ALTER TABLE versions DROP COLUMN path;

CREATE UNIQUE INDEX ix_versions_file ON versions (file_id, version);

-- allow this index to be not unique on an odd chance of a hash collision
CREATE INDEX ix_versions_hash ON versions (hash, hash_type);

--
-- Add a new table for EXIF values
--
CREATE TABLE exif (
    BitsPerSample TEXT NULL, Compression INTEGER NULL, DocumentName TEXT NULL, ImageDescription TEXT NULL, 
    Make TEXT NULL, Model TEXT NULL, Orientation INTEGER NULL, SamplesPerPixel TEXT NULL, 
    Software TEXT NULL, DateTime TEXT NULL, Artist TEXT NULL, Copyright TEXT NULL, 
    ExposureTime TEXT NULL, FNumber TEXT NULL, ExposureProgram INTEGER NULL, ISOSpeedRatings INTEGER NULL, 
    SensitivityType INTEGER NULL, ISOSpeed INTEGER NULL, TimeZoneOffset TEXT NULL, DateTimeOriginal TEXT NULL,
    DateTimeDigitized TEXT NULL, OffsetTime TEXT NULL, OffsetTimeOriginal TEXT NULL, OffsetTimeDigitized TEXT NULL,
    ShutterSpeedValue TEXT NULL, ApertureValue TEXT NULL, SubjectDistance TEXT NULL, BrightnessValue TEXT NULL,
    ExposureBiasValue TEXT NULL, MaxApertureValue TEXT NULL, MeteringMode INTEGER NULL, LightSource INTEGER NULL,
    Flash INTEGER NULL, FocalLength TEXT NULL, UserComment TEXT NULL, SubsecTime TEXT NULL,
    SubSecTimeOriginal TEXT NULL, SubSecTimeDigitized TEXT NULL, FlashpixVersion TEXT NULL, FlashEnergy TEXT NULL,
    SubjectLocation TEXT NULL, ExposureIndex TEXT NULL, SensingMethod INTEGER NULL, SceneType INTEGER NULL,
    ExposureMode INTEGER NULL, WhiteBalance INTEGER NULL, DigitalZoomRatio TEXT NULL, FocalLengthIn35mmFilm TEXT NULL,
    SceneCaptureType INTEGER NULL, SubjectDistanceRange TEXT NULL, ImageUniqueID TEXT NULL, CameraOwnerName TEXT NULL,
    BodySerialNumber TEXT NULL, LensSpecification TEXT NULL, LensMake TEXT NULL, LensModel TEXT NULL,
    LensSerialNumber TEXT NULL, GPSLatitudeRef TEXT NULL, GPSLatitude TEXT NULL, GPSLongitudeRef TEXT NULL,
    GPSLongitude TEXT NULL, GPSAltitudeRef TEXT NULL, GPSAltitude TEXT NULL, GPSTimeStamp TEXT NULL,
    GPSSpeedRef TEXT NULL, GPSSpeed TEXT NULL, GPSDateStamp TEXT NULL);

ALTER TABLE versions ADD COLUMN exif_id INTEGER NULL;

--
-- Set the target database version
--
PRAGMA user_version=30;             -- VER_TO

COMMIT TRANSACTION;
