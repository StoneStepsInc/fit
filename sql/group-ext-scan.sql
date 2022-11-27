--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-ext-scan.sql
--
-- Groups files by extension for the scan equal to N, which
-- is defaulted to the last scan ID, if omitted.
--
SELECT
    ext,
    count(ext) as file_count,
    count(exif_id) as EXIF_count,
    round(sum(entry_size) / 1000., 3) as sum_entry_size_kb,
    round(avg(entry_size) / 1000., 3) as avg_entry_size_kb,
    round(max(entry_size) / 1000., 3) as max_entry_size_kb
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
