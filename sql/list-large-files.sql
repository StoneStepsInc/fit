--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" -cmd ".param set @TOP M" sqlite.db < sql/list-large-files.sql
--
-- Lists large files in the specified scan. If SCAN_ID is omitted,
-- the last one is used. If TOP is omitted, top 10 files will be
-- listed.
--
SELECT
    scan_id,
    file_id,
    version,
    path,
    datetime(mod_time, 'unixepoch') AS mod_time,
    round(entry_size/1000./1000., 3) AS entry_size_mb
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
WHERE
    scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0)
ORDER BY
    entry_size DESC
LIMIT
    coalesce(@TOP, 10)
