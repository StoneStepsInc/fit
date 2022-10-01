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
    version,
    path,
    mod_time,
    round(entry_size/1000., 3) AS entry_size_kb
FROM
    versions
    JOIN files ON versions.file_id = files.rowid
WHERE
    hash = @HASH COLLATE NOCASE
ORDER BY
    path, version DESC;
