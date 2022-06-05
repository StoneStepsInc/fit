select
    version,
    path,
    mod_time,
    entry_size,
    hash_type,
    hash
from files
where hash in (
    select hash
    from files
    group by hash, hash_type
    having count(*) > 1
)
order by hash, path, version desc;
