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

DROP INDEX ix_files_path;
DROP INDEX ix_files_hash;

ALTER TABLE files RENAME TO versions;

ALTER TABLE versions ADD COLUMN file_id INTEGER NOT NULL DEFAULT 0;

CREATE TABLE files (
    name TEXT NOT NULL,
    ext TEXT NULL,
    path TEXT NOT NULL
);

--
-- Extensions may be NULL for older scans and we may end up with
-- duplcate paths if same path has an extension in a newer scan.
-- Allow this to happen and then explicitly delete duplicate paths
-- that have NULL extensions, but leaving original paths that do
-- not have extensions alone (e.g. `.gitignore`).
--
INSERT INTO files (name, ext, path) SELECT DISTINCT name, ext, path FROM versions;

-- delete duplicate paths with a NULL extension and leave actual NULL extensions alone
DELETE FROM files WHERE ext IS NULL AND path IN (SELECT path FROM files GROUP BY path HAVING COUNT(path) > 1);

-- update version records to reference new file records
UPDATE versions SET file_id = files.rowid FROM files WHERE files.path = versions.path;

-- attempt to create a unique path index before we drop those columns
CREATE UNIQUE INDEX ix_files_path ON files (path);

ALTER TABLE versions DROP column name;
ALTER TABLE versions DROP column ext;
ALTER TABLE versions DROP column path;

CREATE UNIQUE INDEX ix_versions_file ON versions (file_id, version);

-- 
CREATE INDEX ix_versions_hash ON versions (hash, hash_type);

--
-- Add a new table for snapshot files
--
CREATE TABLE scansets (
    scan_id INTEGER NOT NULL,
    file_id INTEGER NOT NULL,
    version_id INTEGER NOT NULL
);

CREATE UNIQUE INDEX ix_scansets_scan_file ON scansets (
    scan_id,
    file_id
);

--
-- Set the target database version
--
PRAGMA user_version=30;             -- VER_TO

COMMIT TRANSACTION;
