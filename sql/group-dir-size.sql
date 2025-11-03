--
-- sqlite3 -line -cmd ".param set @SCAN_ID N" -cmd ".param set @DIR_NAME 'D/'" sqlite.db < sql/group-dir-size.sql
--
-- Lists the size of all files under the specified directory.
--
-- @DIR_NAME cannot be omitted and must end in a path separator
-- to avoid including partial matches.
--
-- If @SCAN_ID is not specified, the last one will be used.
--
SELECT
    @DIR_NAME AS dir_name,
    COUNT(file_id) AS file_count,
    MIN(entry_size) AS min_entry_size,
    round(AVG(entry_size) / 1000. / 1000., 3) AS avg_entry_size_mb,
    round(MAX(entry_size) / 1000. / 1000., 3) AS max_entry_size_mb,
    MIN(datetime(mod_time, 'unixepoch')) AS min_mod_time,
    MAX(datetime(mod_time, 'unixepoch')) AS max_mod_time,
    round(SUM(entry_size)/1000./1000./1000., 3) AS dir_size_gb
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0)
    AND path LIKE concat(@DIR_NAME, '%')
GROUP BY
    substring(path, 1, length(@DIR_NAME));
