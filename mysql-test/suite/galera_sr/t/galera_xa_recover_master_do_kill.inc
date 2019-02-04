# Kills the server too which $conn_restart is connected to,
# performs wsrep recovery on it, and finally restarts it.
#
# $conn_restart - name of the connection that will kill the server
# $conn_control - name of the connection that is connection to some
#                 other node in the cluster
# $server_id_restart - id of the server to be killed

--connection $conn_restart
SET SESSION wsrep_sync_wait = 0;
--source include/kill_galera.inc

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
