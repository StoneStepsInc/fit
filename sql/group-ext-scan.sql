--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-ext-scan.sql
--
-- Groups files by extension for the scan equal to N, which
-- is defaulted to the last scan ID, if omitted.
--
SELECT
    ext,
    COUNT(ext) as file_count,
    COUNT(exif_id) as EXIF_count,
    round(SUM(entry_size) / 1000000., 3) AS sum_entry_size_mb,
    round(AVG(entry_size) / 1000000., 3) AS avg_entry_size_mb,
    round(MAX(entry_size) / 1000000., 3) AS max_entry_size_mb
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
WHERE
    scansets.scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0) AND
    ext IS NOT NULL
GROUP BY
    scansets.scan_id,
    ext COLLATE NOCASE
ORDER BY
    scansets.scan_id, 5 DESC
