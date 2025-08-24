create table users (id int, username string, email string)
use users
insert into users 1 alice alice@example.com
insert into users 2 bob bob@example.com
select * from users
select id, username from users where id = 1
.exit
