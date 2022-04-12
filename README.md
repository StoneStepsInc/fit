# File Integrity Tracker (fit)

`fit` is a command line application that computes file checksums
for a file tree and can verify file integrity against the same
tree at the same or a different base directory path.

`fit` can run in two modes - to scan a file tree and record file
checksums in a SQLite database and to scan the same file tree,
possibly at a different base file path, in order to verify file
checksums against those stored in the SQLite database.

## File Tree Scanning Options

The options to scan a file tree are the same for both scanning
modes and are as follows:

  * `-d c:\path\to\target`

    Points to the directory to scan. If omitted, current directory
    is scanned.

  * `-p c:\path\to`

    Specifies the base path that will be removed from file paths
    under the the scanned directory when stored in the database.

    If omitted, full source path will be recorded in the database
    and only the original file tree can be verified.

    For example, if a user stores their pictures and videos under
    `C:\Users\Alice\Pictures` and `C:\Users\Alice\Videos`, and
    these directories are copied to an external drive, under
    `X:\Alice\`, then a base path `C:\Users\Alice` will be used
    to record checksums for pictures and videos and a base path
    `X:\Alice` will be used to verify file copies on the external
    drive.

  * `-b c:\path\to\database\file`

    Points to a SQLite database file `fit` will use to record and
    verify file checksums.

  * `-r`

    When specified, tells `fit` to scan the directory named via
    `-d` and all of its sub-directories.

  * `-w`

    This option instructs `fit` to use the file modification time
    as an indicator that the file was not changed, rather than to
    compute its checksum. This option is intended to recover from
    an interrupted scan, without having to compute checksums
    for all files that have been processed when Ctrl-C was pressed.

## Scanning a File Tree

Scanning a file tree without the `-v` option will record computed
checksums in the specified SQLite database.

Multiple directories may be recorded in the same SQLite database.
For example, for a drive that stores pictures and videos in separate
root folders, `X:\Pictures` and `X:\Videos`, scanning those folders
individually avoids scanning system folders `System Volume Information`
and `$RECICLE.BIN`.

Each file tree scan is recorded separately in the SQLite database
and may be accompanied by a text message using the `-m` option,
which may be useful when reviewing scan records later.

## Verifying a File Tree

Scanning a file tree with the `-v` option will compare computed
checksums against those stored during the scan phase.

Files with mismatching or missing checksums will be reported with
three labels:

  * `New file`

    This file was included in a verification file tree scan, but
    is not found in the database.

  * `Modified`

    This file was found in the database and its new checksum does
    not match that of the database record and the current file
    modification time is not equal to the one in the database
    record.

    The current file modification time may be ahead or behind the
    database time if a file was modified or restored from a backup.
    No distinction is made between these two cases.

  * `Changed`

    This file was found in the database and its new checksum does
    not match that of the database record, the current file
    modification time is the same as the one in the database
    record.

    This means that the file changed outside of the usual file
    editing applications, which typically update file modification
    time. For example, disk corruption or direct disk access may
    change file contents without updating file modification time.

## Scan Performance

`fit` scans file trees using multiple threads, but actual scan
speed will depend much on the type of disks being scanned and
may controlled to some degree with the `-t` option, which
specifies how many threads should be used for scanning.

For disks that allow fast random access, such as SSD drives, more
threads will result in better performance. For example, scanning
an internal SSD drive yields these scan times, depending on the
number of threads used.

  * `-t 2` scans at `200.7` MB/sec
  * `-t 16` scans at `246.9` MB/sec
  * `-t 32` scans at `240.4` MB/sec

For magnetic disks, more threads reading the disk results in
more contension and using fewer threads results in better scan
speed. The numbers below are captured against a dual-drive
Storage Space attached via USB 3.0 connected to a Thunderbolt
port.

  * `-t 1` scans at `76.9` MB/sec
  * `-t 2` scans at `95.2` MB/sec
  * `-t 16` scans at `58.7` MB/sec
  * `-t 32` scans at `51.8` MB/sec

## SQLite Database

The SQLite database contains two tables, one for scans and one
for files.

All text fields are stored as UTF-8 characters. Note that all
text comparisons in SQLite are case-sensitive and `ABC` will not
compare equal to `abc`. Moreover, if case-insensitive collation
is used in queries, it will only work with ASCII characters and
will not apply across all Unicode characters.

### Scan Table

The `scan` table contains a record for each successful run of `fit`
without the `-v` option and has the following fields:

  * `rowid` `INTEGER NOT NULL`

    SQLite maintains this column automatically and it is not
    explicitly included in the schema. It has to be included
    explicitly in the `select` statement to be visibe in
    results (e.g. `select rowid, * from scans;`).

  * `app_version` `TEXT NOT NULL`

    Application version that generated this scan record.

  * `timestamp` `INTEGER NOT NULL`
    
    Number of seconds since 1970-01-01, UTC. Use `datetime(timestamp,
    'unixepoch')` to output as a calendar time in the SQLite shell.

  * `scan_path` `TEXT NOT NULL`

    The scan path, derived from the `-d` option.

  * `base_path` `TEXT NOT NULL`

    The base path, derived from the `-p` option.

  * `current_path` `TEXT NOT NULL`

    The current path recorded when `fit` was launched.

  * `message` `TEXT`

    A text message to decribe this scan. If omitted, `NULL` is stored.

### Files Table

The files table contains a record per scanned file and has following
fields:

  * `scan_id` `INTEGER NOT NULL`

    Scan record identifier (`scan.rowid`).

  * `version` `INTEGER NOT NULL`

    File record version. Starts with `1` and incremented every time a
    new hash value is computed for a file path. Previous file version
    are kept intact.

    The last version is always used when comparing file checksums
    during scans.

  * `name` `TEXT NOT NULL`

    File name without file path. This field is only useful for file
    name queries and it will contain numerous duplicates across
    multiple file path versions and files with the same name located
    in different directories.

    This field is not indexed and a full table scan will be performed
    for every query that uses the file name as the only criteria. It
    is useful for file name queries to avoid a `LIKE` clause against
    the file path that compares just the file name at the end of the
    path.

  * `path` `TEXT NOT NULL`

    A file path with the base path removed, if a base path is used.

    File paths are versioned and the latest version should be selected
    to obtain the record for the most recent scan.
    
  * `mod_time` `INTEGER  NOT NULL`

    File modification time in platform-specific units. It should not
    be assumed to be a Unix epoch time stamp. For example, on Windows
    this value represents 100 nanosecond units since 1601-01-01, UTC
    and may be converted to Unix epoch by subtracting the value for
    1970-01-01, UTC from the file time, which translates into this
    SQLite clause - `datetime(mod_time-11644473600, 'unixepoch')`.

  * `entry_size` `INTEGER NOT NULL`

    A file size, in bytes, as reported by a directory entry for this
    file.

  * `read_size` `INTEGER NOT NULL`

    A file size, in bytes, as computed by reading the file until
    there is no more data. In most cases this value will be
    exactly as stored in `entry_size`, unless either the file or
    the directory entry was updated after it was read by the file
    scanner.

  * `hash_type` `VARCHAR(32) NOT NULL`

    File checksums are computed as a SHA-256 hash in the current
    application, so this column will always be set to `SHA256`.

  * `hash` `TEXT NOT NULL`

    A file checksum value in hex format using lowercase characters
    for letters `abcdef`, which is computed as a SHA-256 hash in
    the current application.

### Useful SQL

You can run SQL queries against the SQLite database using SQLite
shell included in the application package. 

#### Latest File Checksum

    select path, hash
    from files
    where path = 'path\to\file'
    order by version desc
    limit 1;

#### Select Files by Checksum

    select 
        version,
        path,
        datetime(mod_time-11644473600, 'unixepoch') as mod_time,
        entry_size
    from files
    where hash = 'hash-value' COLLATE NOCASE
    order by path, version desc;

SHA-256 hash may be computed on Windows using `certutil` as follows:

    certutil -hashfile path\to\file SHA256

#### Select Duplicate Files

    select
        version,
        path,
        datetime(mod_time-11644473600, 'unixepoch') as mod_time,
        entry_size
    from files
    where hash in (
        select hash
        from files
        group by hash
        having count(hash) > 1
        order by path, version desc
    )
    order by path, version desc;

## Source

Current source requires Visual Studio 2019 and requires SQLite in
a Nuget package, as well as a library to compute SHA-256 hashes.

The latter does not have a package and may be obtained via a batch
file included in the project (`get-sha256.bat`). After running
this batch file, a directory `sha256` will contain the source for
the SHA-256 library and this Visual Studio project will build.

## License

This application is licensed under BSD-3 terms. Read the `LICENSE`
file in the application package for details.

### 3rd-Party Code

This application uses following 3rd-party libraries, licensed separately.

#### SQLite

A SQL database management library.

LICENSE: [Pubic Domain](https://www.sqlite.org/copyright.html)

#### SHA256

An implementation of the SHA-256 secure hash algorithm

LICENSE: [MIT](https://github.com/ilvn/SHA256/blob/main/LICENSE)

