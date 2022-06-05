--
-- sqlite3 -line sqlite.db < sql/list-dup-files.sql
--
-- On Windows mod_time may be shown as a date/time value using
-- this expression:
--
--   datetime(mod_time-11644473600, 'unixepoch') as mod_time
--
SELECT
    version,
    path,
    mod_time,
    entry_size,
    hash_type,
    hash
FROM
    files
WHERE hash IN (
    SELECT hash
    FROM files
    GROUP BY hash, hash_type
    HAVING count(*) > 1
)
ORDER BY
    hash, path, version DESC;
