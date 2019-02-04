# Issues one more XA transaction and uses debug crash point
# crash_replicate_fragment_after_certify. Meaning that the server
# will crash after it has replicated the XA PREPARE fragment,
# and before the transaction is prepared locally in innodb.
# After the crash, performs wsrep recovery step and finally
# restart the server.
#
# $conn_restart - name of the connection that will kill the server
# $conn_control - name of the connection that is connection to some
#                 other node in the cluster
# $server_id_restart - id of the server to be killed

--connection $conn_restart
--let $_expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.$server_id_restart.expect
--exec echo "wait" > $_expect_file_name

SET GLOBAL debug_dbug="d,crash_replicate_fragment_after_certify";
XA START 'crash';
INSERT INTO t1 VALUES(999);
XA END 'crash';
--error 2013 # server lost
XA PREPARE 'crash';

--connection $conn_control
SET SESSION wsrep_sync_wait = 0;
--let $wait_condition = SELECT VARIABLE_VALUE = 1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'wsrep_cluster_size';
--source include/wait_condition.inc

--connection $conn_restart
--let $galera_wsrep_recover_server_id=$server_id_restart
--source suite/galera/include/galera_wsrep_recover.inc

--let $_expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.$server_id_restart.expect
--source include/start_mysqld.inc

--let $wait_condition = SELECT VARIABLE_VALUE = 2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'wsrep_cluster_size';
--source include/wait_condition.inc

--connection $conn_control
--let $wait_condition = SELECT VARIABLE_VALUE = 2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'wsrep_cluster_size';
--source include/wait_condition.inc

--echo Expect transactions 't1', 't2', and 'crash'
XA RECOVER;
SELECT xid FROM mysql.wsrep_streaming_log;

--connection $conn_restart
--echo Expect transactions 't1', 't2', and 'crash'
XA RECOVER;
SELECT xid FROM mysql.wsrep_streaming_log;
XA ROLLBACK 'crash';

SELECT * FROM t1;

--connection $conn_control
SELECT * FROM t1;