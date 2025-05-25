--
-- This script upgrades the database schema from version 6.0 to
-- version 7.0.
--
-- sqlite3 sqlite.db < upgrade-db_7.0-8.0.sql
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
    WHEN 70 THEN 1                          -- VER_FROM
    ELSE NULL
  END
);

--                                             VER_TO
.print Upgrading database to version 8.0

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
  70,                                       -- VER_TO
  CAST(strftime('%s', 'now') AS INTEGER)
);

ALTER TABLE scans ADD COLUMN last_update_time INTEGER NULL;
ALTER TABLE scans ADD COLUMN cumulative_duration INTEGER NULL;
ALTER TABLE scans ADD COLUMN times_updated INTEGER NULL;

--
-- Set the target database version
--
PRAGMA user_version=80;                     -- VER_TO

COMMIT TRANSACTION;
