--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exiv2json-focallength35mm.sql
--
-- Groups images by $.Exif.Photo.FocalLengthIn35mmFilm field in
-- Exiv2Json, rounded up/down to the nearest multiple of 5.
-- If @SCAN_ID is omitted, the last scan ID is used.
--
SELECT
    CAST(round(json_extract(Exiv2Json, '$.Exif.Photo.FocalLengthIn35mmFilm')/5., 0)*5 AS INTEGER) AS FocalLengthIn35mm,
    COUNT(*)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0) AND
    FocalLengthIn35mm IS NOT NULL
GROUP BY
    CAST(round(json_extract(Exiv2Json, '$.Exif.Photo.FocalLengthIn35mmFilm')/5., 0)*5 AS INTEGER)
ORDER BY
    2 DESC
