--
-- Stop and exit if any statement triggered an error.
--
.bail on

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

INSERT INTO upgrades (
  upgrade_from,
  upgrade_to,
  upgrade_time
) VALUES (
  (select user_version from pragma_user_version()),
  10,
  unixepoch()
);

--
-- Change base_path to allow NULL values
--
ALTER TABLE scans ADD COLUMN base_path_new TEXT NULL;

UPDATE scans SET base_path_new=(CASE length(base_path) WHEN 0 THEN NULL ELSE base_path END);

ALTER TABLE scans DROP COLUMN base_path;
ALTER TABLE scans RENAME COLUMN base_path_new TO base_path;

--
-- Set the target database version
--
PRAGMA user_version=10;
