create table images (id int, device_id string, created_at timestamp, hash string, blob_key string, description string)
use images
insert into images 1 device001 1696636800 hash1 blobkey1 ''
insert into images 2 device001 1696636801 hash2 blobkey2 ''
select id, device_id, created_at, hash, blob_key, description from images order by created_at desc
select id from images order by id desc limit 1 offset 0
delete from images where id = 1
delete from images where id = 2
select id, device_id, created_at, hash, blob_key, description from images order by created_at desc
select id from images order by id desc limit 1 offset 0
insert into images 3 device001 1696636802 hash3 blobkey3 ''
select id, device_id, created_at, hash, blob_key, description from images order by created_at desc

