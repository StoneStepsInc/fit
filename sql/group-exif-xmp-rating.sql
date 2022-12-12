--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-xmp-rating.sql
--
-- Groups files with EXIF by Make and Model in EXIF for scans equal
-- to N, which is defaulted to last scan ID, if omitted.
--
SELECT
    XMPxmpRating,
    COUNT(exif_id),
    round(SUM(entry_size) / 1000000., 3) AS sum_entry_size_mb,
    round(AVG(entry_size) / 1000000., 3) AS avg_entry_size_mb,
    round(MAX(entry_size) / 1000000., 3) AS max_entry_size_mb
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (select MAX(rowid) FROM scans), 0)
GROUP BY
    XMPxmpRating
ORDER BY
    XMPxmpRating DESC
