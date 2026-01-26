--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" -cmd ".param set @TOP M" sqlite.db < sql/list-large-dirs.sql
--
-- Lists large directories in the specified scan. If SCAN_ID is omitted,
-- the last one is used. If TOP is omitted, top 10 directories will be
-- listed.
--
SELECT
    MAX(scan_id),
    substring(path, 1, length(path)-length(name)-1) AS dir,
    round(SUM(entry_size)/1000./1000., 3) AS dir_size_mb,
    COUNT(file_id) AS file_count
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
WHERE
    scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0)
GROUP BY
    substring(path, 1, length(path)-length(name)-1)
ORDER BY
    SUM(entry_size) DESC
LIMIT
    coalesce(@TOP, 10)
