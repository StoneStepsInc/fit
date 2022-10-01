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
    version,
    path,
    mod_time,
    round(entry_size/1000., 3) as entry_size_kb,
    hash
FROM
    versions
    JOIN files ON versions.file_id = files.rowid
WHERE
    name = @FILENAME
ORDER BY
    name, version DESC;
