--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-ext-scan.sql
--
-- Groups files by extension for scans greater than or equal to N,
-- which is defaulted to 1, if omitted.
--
-- On Windows mod_time may be shown as a date/time value using
-- this expression:
--
--   datetime(mod_time-11644473600, 'unixepoch') as mod_time
--
SELECT
    scansets.scan_id,
    ext,
    count(ext) as count,
    round(sum(entry_size) / 1000., 3) as entry_size_kb,
    round(avg(entry_size) / 1000., 3) as avg_entry_size_kb,
    round(max(entry_size) / 1000., 3) as max_entry_size_kb
FROM
    scansets
    JOIN files ON scansets.file_id = files.rowid
    JOIN versions ON scansets.version_id = versions.rowid
WHERE
    scansets.scan_id >= coalesce(@SCAN_ID, 1) AND
    ext IS NOT NULL
GROUP BY
    scansets.scan_id,
    ext
ORDER BY
    scansets.scan_id DESC, 4 DESC
