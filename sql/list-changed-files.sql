--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" -cmd ".param set @BASE_SCAN_ID B" sqlite.db < sql/list-changed-files.sql
--
-- Lists added and changed files between scans B and N. If BASE_SCAN_ID
-- is omitted, N-1 is used. If both are omitted, the last two scans are
-- used.
--
-- Files with version 1 in the last scan are added files and files with
-- other version values were changed between specified scans. Files in
-- the previous scan are removed.
--
SELECT
    scan_id,
    file_id,
    version,
    path,
    datetime(mod_time, 'unixepoch') AS mod_time,
    entry_size,
    hash_type,
    hash
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
WHERE scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0)
    AND version_id NOT IN (
        SELECT
            version_id
        FROM 
            scansets
        WHERE
            scan_id = coalesce(@BASE_SCAN_ID, @SCAN_ID-1, (SELECT MAX(rowid) FROM scans)-1, 0)
    )
ORDER BY path
