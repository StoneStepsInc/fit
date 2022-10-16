--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-camera.sql
--
-- Groups files with EXIF by Make and Model in EXIF for scans equal
-- to N, which is defaulted to 1, if omitted.
--
SELECT
    Make,
    Model,
    count(*)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, 1)
GROUP BY
    exif.Make COLLATE NOCASE, exif.Model COLLATE NOCASE
ORDER BY
    count(*)
