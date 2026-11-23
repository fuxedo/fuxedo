set echo off
drop table fuxedo_xa_test purge;
create table fuxedo_xa_test (id number primary key, tag varchar2(64), status varchar2(16) default 'new', created timestamp default systimestamp);
delete from fuxedo_xa_test;
insert into fuxedo_xa_test (id, tag) values (1, 'setup');
commit;
exit
