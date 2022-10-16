--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-ext-scan.sql
--
-- Groups files by extension for the scan equal to N, which
-- is defaulted to 1, if omitted.
--
SELECT
    ext,
    count(ext) as count,
    round(sum(entry_size) / 1000., 3) as entry_size_kb,
    round(avg(entry_size) / 1000., 3) as avg_entry_size_kb,
    round(max(entry_size) / 1000., 3) as max_entry_size_kb
FROM
    scansets
    JOIN versions ON version_id = versions.rowid
    JOIN files ON file_id = files.rowid
WHERE
    scansets.scan_id = coalesce(@SCAN_ID, 1) AND
    ext IS NOT NULL
GROUP BY
    scansets.scan_id,
    ext COLLATE NOCASE
ORDER BY
    scansets.scan_id, 4 DESC
