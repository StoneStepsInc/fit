--
-- sqlite3 -box -cmd ".param set @SCAN_ID N" sqlite.db < sql/group-exif-exp-prog.sql
--
-- Groups files with EXIF by ExposureProgram for scans equal to N,
-- which is defaulted to 1, if omitted.
--
-- See EXIF specification for numeric value descriptions.
--
-- http://www.cipa.jp/std/documents/e/DC-008-2012_E.pdf
--
SELECT
    CASE (ExposureProgram)
        WHEN 0 THEN 'Undefined'
        WHEN 1 THEN 'Manual'
        WHEN 2 THEN 'Normal program'
        WHEN 3 THEN 'Aperture priority'
        WHEN 4 THEN 'Shutter priority'
        WHEN 5 THEN 'Creative program (depth of field)'
        WHEN 6 THEN 'Action program (shutter speed)'
        WHEN 7 THEN 'Portrait mode'
        WHEN 8 THEN 'Landscape mode'
        ELSE 'Unknown'
    END,
    count(*)
FROM 
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, 1)
GROUP BY
    ExposureProgram
ORDER BY
    count(*) DESC
