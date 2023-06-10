--
-- sqlite3 -box -cmd ".param set @FILEPATH some-file-path" -cmd ".param set @SCAN_ID N" sqlite.db < sql/list-exiv2json-fields.sql
--
-- Lists all Exiv2Json fields for the specified file and scan ID.
-- If @SCAN_ID is omitted, the last scan ID is used.
--
SELECT
    file_id,
    json_field.fullkey,
    json_field.value
FROM 
    json_tree(Exiv2Json) AS json_field,
    scansets
    JOIN versions ON version_id = versions.rowid 
    JOIN files ON file_id = files.rowid 
    JOIN exif ON exif_id = exif.rowid
WHERE
    scan_id = coalesce(@SCAN_ID, (SELECT MAX(rowid) FROM scans), 0)
    AND files.path = @FILEPATH
    AND json_field.type <> 'object'                  -- skip sub-object JSON values
    AND typeof(json_field.key) <> 'integer'          -- skip individual array values
    AND json_field.fullkey NOT LIKE '$."_fit".%'     -- skip our fields (e.g. overized array names, etc)
ORDER BY
    file_id, json_field.id