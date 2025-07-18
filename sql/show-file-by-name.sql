--
-- sqlite3 -line -cmd ".param set @FILENAME some-file-name" sqlite.db < sql/show-file-by-name.sql
--
-- Lists all file versions with the given file name.
--
-- On Windows mod_time may be shown as a date/time value using
-- this expression:
--
--   datetime(mod_time-11644473600, 'unixepoch') as mod_time
--
SELECT
    scan_id,
    version,
    path,
    mod_time,
    round(entry_size/1000000., 3) AS entry_size_mb,
    hash,
    message
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
    JOIN scans on scan_id = scans.rowid
WHERE
    name = @FILENAME COLLATE NOCASE
ORDER BY
    name, version DESC;
