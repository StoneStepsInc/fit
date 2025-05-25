--
-- sqlite3 -line sqlite.db < sql/list-scans.sql
--
-- Lists all scans and computes scan aggregates.
--
SELECT
  MAX(app_version) AS app_version,
  scans.rowid AS scan_id,
  datetime(MAX(scan_time), 'unixepoch') AS scan_time,
  datetime(MAX(last_update_time), 'unixepoch') AS last_update_time,
  datetime(MAX(completed_time), 'unixepoch') AS completed_time,
  round(coalesce(cumulative_duration, 0)/60., 3) as scan_duration_mn,
  times_updated,
  COUNT(version_id) AS file_count,
  COUNT(exif_id) AS EXIF_count,
  round(SUM(entry_size) / 1000000., 3) AS sum_entry_size_mb,
  round(AVG(entry_size) / 1000000., 3) AS avg_entry_size_mb,
  round(MAX(entry_size) / 1000000., 3) AS max_entry_size_mb,
  MAX(base_path) AS base_path,
  MAX(options) as options,
  MAX(message) AS message
FROM 
  scans
  LEFT JOIN scansets ON scan_id = scans.rowid
  LEFT JOIN versions ON version_id = versions.rowid 
GROUP BY
    scans.rowid
ORDER BY
    scans.rowid;
