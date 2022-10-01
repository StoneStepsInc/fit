--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" sqlite.db < sql/list-dup-files.sql
--
-- Lists duplicate files for scans greater than or equal to N,
-- which is defaulted to 1, if omitted.
--
-- On Windows mod_time may be shown as a date/time value using
-- this expression:
--
--   datetime(mod_time-11644473600, 'unixepoch') as mod_time
--
SELECT
    oss.scan_id,
    oss.file_id,
    version,
    path,
    mod_time,
    entry_size,
    hash_type,
    hash
FROM
    scansets oss
    JOIN files ON oss.file_id = files.rowid
    JOIN versions ON oss.version_id = versions.rowid
WHERE
    oss.scan_id >= coalesce(@SCAN_ID, 1) AND
    hash IN (
        SELECT hash
        FROM 
            scansets iss
            JOIN versions ON iss.version_id = versions.rowid
        WHERE oss.scan_id = iss.scan_id
        GROUP BY iss.scan_id, hash, hash_type
        HAVING count(*) > 1
    )
ORDER BY
    hash, path, version DESC;
