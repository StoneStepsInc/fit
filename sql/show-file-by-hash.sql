--
-- sqlite3 -line -cmd ".param set @HASH hash-value" sqlite.db < sql/show-file-by-hash.sql
--
-- Lists all file versions with the given hash value.
--
-- On Windows mod_time may be shown as a date/time value using
-- this expression:
--
--   datetime(mod_time-11644473600, 'unixepoch') as mod_time
--
-- A SHA-256 hash may be computed on Windows using `certutil:
--
--   certutil -hashfile path\to\file SHA256
--
SELECT 
    scan_id,
    version,
    path,
    mod_time,
    round(entry_size/1000000., 3) AS entry_size_mb,
    message
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
    JOIN scans on scan_id = scans.rowid
WHERE
    hash = @HASH COLLATE NOCASE
ORDER BY
    path, version DESC;
