--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-camera.sql
--
-- Groups files with EXIF by year/month of when the photo was taken
-- and camera model for scans equal to N, which is defaulted to the
-- last scan ID, if omitted.
--
SELECT
    strftime('%Y-%m', DateTimeOriginal) AS DateTimeOriginal,
    Model,
    COUNT(exif_id)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0) AND
    DateTimeOriginal IS NOT NULL
GROUP BY
    strftime('%Y-%m', DateTimeOriginal)
ORDER BY
    1, Model, COUNT(exif_id) DESC
