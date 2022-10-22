--
-- sqlite3 -line sqlite.db < sql/list-scans.sql
--
-- Lists all scans and computes scan aggregates.
--
SELECT
  scans.rowid AS scan_id,
  datetime(MAX(scan_time), 'unixepoch') AS scan_time,
  MAX(app_version) AS app_version,
  COUNT(version_id) AS file_count,
  COUNT(exif_id) AS EXIF_count,
  round(SUM(entry_size) / 1000., 3) AS entry_size_kb,
  round(AVG(entry_size) / 1000., 3) AS avg_entry_size_kb,
  round(MAX(entry_size) / 1000., 3) AS max_entry_size_kb,
  MAX(scan_path) AS scan_path,
  MAX(base_path) AS base_path,
  MAX(current_path) AS current_path,
  MAX(options) as options,
  MAX(message) AS message
FROM 
  scans
  JOIN scansets ON scan_id = scans.rowid
  JOIN versions ON version_id = versions.rowid 
GROUP BY
    scans.rowid
ORDER BY
    scans.rowid;
