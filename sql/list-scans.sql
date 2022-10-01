--
-- sqlite3 -line sqlite.db < sql/list-scans.sql
--
-- Lists all scans and computes scan aggregates.
--
SELECT
  scans.rowid AS scan_id,
  datetime(MAX(scan_time), 'unixepoch') AS scan_time,
  MAX(app_version) AS app_version,
  COUNT(DISTINCT scansets.file_id) AS version_count,
  COUNT(scansets.file_id) AS file_count,
  round(SUM(entry_size) / 1000., 3) AS entry_size_kb,
  round(AVG(entry_size) / 1000., 3) AS avg_entry_size_kb,
  round(MAX(entry_size) / 1000., 3) AS max_entry_size_kb,
  MAX(scan_path) AS scan_path,
  MAX(base_path) AS base_path,
  MAX(current_path) AS current_path,
  MAX(message) AS message
FROM 
  scans
  JOIN scansets ON scans.rowid = scansets.scan_id
  JOIN versions ON scansets.version_id = versions.rowid 
GROUP BY scans.rowid
ORDER BY 1 DESC;
