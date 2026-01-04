--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" -cmd ".param set @BASE_SCAN_ID B" sqlite.db < sql/list-moved-files.sql
--
-- Lists files in N that were moved between scans B and N.
-- If BASE_SCAN_ID is omitted, N-1 is used. If both are omitted,
-- the last two scans are used.
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
    -- a current scan file is moved if its file_id, which is synonymous to path, does not exist in the base scan, and ...
    AND file_id NOT IN (
        SELECT
            file_id
        FROM 
            scansets
            JOIN versions on version_id = versions.rowid
        WHERE
            scan_id = coalesce(@BASE_SCAN_ID, @SCAN_ID-1, (SELECT MAX(rowid) FROM scans)-1, 0)
    )
    -- ... its hash does exist in the base scan (hashes are globally unique - no need to complicate the query with checking hash_type)
    AND hash IN (
        SELECT
            hash
        FROM 
            scansets
            JOIN versions on version_id = versions.rowid
        WHERE
            scan_id = coalesce(@BASE_SCAN_ID, @SCAN_ID-1, (SELECT MAX(rowid) FROM scans)-1, 0)
    )
ORDER BY path
