--
-- sqlite3 -line sqlite.db < sql/list-scans.sql
--
select
  scans.rowid as scan_id,
  datetime(scan_time, 'unixepoch') as scan_time,
  app_version,
  count(files.rowid) as file_count,
  round(sum(entry_size) / 1000., 3) as entry_size_kb,
  round(avg(entry_size) / 1000., 3) as avg_entry_size_kb,
  scan_path,
  base_path,
  current_path,
  message
from 
  scans join files on scans.rowid = files.scan_id 
group by files.scan_id 
order by 1 desc;
