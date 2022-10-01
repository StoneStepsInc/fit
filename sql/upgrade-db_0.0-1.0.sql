--
-- This script upgrades the database schema from the initial 0.0 to
-- version 1.0.
--
-- sqlite3 sqlite.db < upgrade-db_0.0-1.0.sql
--
-- The script uses the DROP COLUMN statement, which was introduced
-- in 3.35.0 (March of 2021), and will fail in earlier SQLite
-- versions. DROP COLUMN may be replaced with RENAME COLUMN for
-- older versions of SQLite.
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
    WHEN 0 THEN 1
    ELSE NULL
  END
);

.print Upgrading database to version 1.0

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
  10,
  CAST(strftime('%s', 'now') AS INTEGER)
);

--
-- Change base_path to allow NULL and update zero-length paths to NULL
--
ALTER TABLE scans ADD COLUMN base_path_new TEXT;

UPDATE scans SET base_path_new=(CASE length(base_path) WHEN 0 THEN NULL ELSE base_path END);

ALTER TABLE scans DROP COLUMN base_path;
ALTER TABLE scans RENAME COLUMN base_path_new TO base_path;

--
-- Change hash to allow NULL and update zero-length file hashes to NULL
--
ALTER TABLE files ADD COLUMN hash_new TEXT;

UPDATE files SET hash_new=(CASE entry_size WHEN 0 THEN NULL ELSE hash END);

DROP INDEX ix_files_hash;

ALTER TABLE files DROP COLUMN hash;
ALTER TABLE files RENAME COLUMN hash_new TO hash;

CREATE INDEX ix_files_hash ON files (hash, hash_type);

--
-- Set the target database version
--
PRAGMA user_version=10;

COMMIT TRANSACTION;
