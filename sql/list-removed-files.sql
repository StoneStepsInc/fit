--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" [-cmd ".param set @BASE_SCAN_ID B"] sqlite.db < sql/list-removed-files.sql
--
-- Lists files removed between scans B and N. If BASE_SCAN_ID is omitted,
-- N-1 is used.
--
SELECT
    scan_id,
    file_id,
    version,
    path,
    mod_time,
    entry_size,
    hash_type,
    hash
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
WHERE scan_id = coalesce(@BASE_SCAN_ID, @SCAN_ID-1, 0)
    AND file_id not IN (
        SELECT
            file_id
        FROM 
            scansets
            JOIN versions on version_id = versions.rowid
        WHERE
            scan_id = coalesce(@SCAN_ID, 1)
    )
ORDER BY path
