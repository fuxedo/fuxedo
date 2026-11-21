set echo off
create table fuxedo_xa_test (id number primary key, tag varchar2(64), created timestamp default systimestamp);
delete from fuxedo_xa_test;
insert into fuxedo_xa_test values (1, 'setup', systimestamp);
commit;
exit
