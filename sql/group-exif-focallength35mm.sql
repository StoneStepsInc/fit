--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-camera.sql
--
-- Groups files with EXIF by FocalLengthIn35mmFilm rounded to the
-- nearest multiple of 5 for scans equal to N, which is defaulted
-- to the last scan ID, if omitted.
--
SELECT
    CAST(round(CAST(FocalLengthIn35mmFilm AS REAL)/5., 0)*5 AS INTEGER) AS FocalLengthIn35mm,
    COUNT(exif_id)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0)
    AND FocalLengthIn35mmFilm IS NOT NULL
GROUP BY
    CAST(round(CAST(FocalLengthIn35mmFilm AS REAL)/5., 0)*5 AS INTEGER)
ORDER BY
    COUNT(exif_id) DESC
