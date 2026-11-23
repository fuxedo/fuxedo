set heading off
select 'ROW s1 ' || count(*) from fuxedo_xa_test where tag = 's1';
select 'ROW s3 ' || count(*) from fuxedo_xa_test where tag = 's3';
select 'ROW s5 ' || count(*) from fuxedo_xa_test where tag = 's5';
select 'ROW s2 ' || count(*) from fuxedo_xa_test where tag = 's2';
select 'ROW s4 ' || count(*) from fuxedo_xa_test where tag = 's4';
select 'ROW s6 ' || count(*) from fuxedo_xa_test where tag = 'slow' and id = 106;
select 'ROW s7 ' || count(*) from fuxedo_xa_test where tag = 's7';
select 'ROW s8 ' || count(*) from fuxedo_xa_test where tag = 's8';
select 'ROW q  ' || count(*) from fuxedo_xa_test where tag = 'q';
select 'PROC q' || count(*) from fuxedo_xa_test where tag = 'q' and status = 'processed';
select 'ROW qr ' || count(*) from fuxedo_xa_test where tag = 'qretry';
select 'PROC qr' || count(*) from fuxedo_xa_test where tag = 'qretry' and status = 'processed';
exit
