--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exiv2json-focallength35mm.sql
--
-- Groups images by $.Exif.Photo.FocalLengthIn35mmFilm field in
-- Exiv2Json, rounded up/down to the nearest multiple of 10.
-- If @SCAN_ID is omitted, the last scan ID is used.
--
SELECT
    COUNT(*),
    cast(round(json_extract(Exiv2Json, '$.Exif.Photo.FocalLengthIn35mmFilm')/10., 0)*10 as INTEGER) as FocalLengthIn35mm
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0)
GROUP BY
    cast(round(json_extract(Exiv2Json, '$.Exif.Photo.FocalLengthIn35mmFilm')/10., 0)*10 as INTEGER)
ORDER BY
    1 DESC
