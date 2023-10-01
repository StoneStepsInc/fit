--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-camera.sql
--
-- Groups files with EXIF by Make and Model in EXIF for scans equal
-- to N, which is defaulted to the last scan ID, if omitted.
--
SELECT
    Model,
    FocalLength,
    COUNT(exif_id)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0)
GROUP BY
    exif.Model COLLATE NOCASE, exif.FocalLength
ORDER BY
    COUNT(exif_id) DESC
