--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-top-dir.sql
--
-- Groups files by top directory for the scan equal to N, which
-- is defaulted to the last scan ID, if omitted.
--
-- This script works only for scans that have a base path, which
-- means that their paths are relative and don't start with a
-- directory separator or with a drive letter.
--
SELECT
    substring(path, 1, MAX(instr(path, '\'), instr(path, '/'))) AS top_dir,
    COUNT(name) AS file_count,
    COUNT(ext) AS ext_count,
    COUNT(exif_id) AS EXIF_count,
    round(SUM(entry_size) / 1000000., 3) AS sum_entry_size_mb,
    round(AVG(entry_size) / 1000000., 3) AS avg_entry_size_mb,
    round(MAX(entry_size) / 1000000., 3) AS max_entry_size_mb
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
WHERE
    scansets.scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0)
GROUP BY
    substring(path, 1, MAX(instr(path, '\'), instr(path, '/')))
ORDER BY
    scansets.scan_id, 5 DESC
