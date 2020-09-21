#
# Crashes the server while there is a prepared XA transaction
# that is replaying.
#
# Parameters:
#
# $sync_point_name - Name of the sync point that will
#                    crash the server during replay
#

--connection node_1
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=true';

--connection node_2
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=true';

#
# Set the the debug sync point that will crash the server
#
--connect conn_restart, 127.0.0.1, root, , test, $NODE_MYPORT_2
--connection conn_restart
--let $_expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.2.expect
--exec echo "wait" > $_expect_file_name
--eval SET GLOBAL debug_dbug="d,$sync_point_name";


#
# Issue a XA transaction 't' up to PREPAREd state
#
--connect trx, 127.0.0.1, root, , test, $NODE_MYPORT_2
--connection trx
CREATE TABLE t1 (f1 INTEGER PRIMARY KEY AUTO_INCREMENT, f2 CHAR(255) , KEY(f2)) ENGINE=InnoDB;
INSERT INTO t1 VALUES (1, 'a'), (2, 'b'), (4, 'd'), (5, 'f');

XA START 't';
UPDATE t1 SET f2 = 'x' WHERE f1 < 3;
XA END 't';
XA PREPARE 't';


#
# The following DELETE will cause 't' to be replayed
#
--connection node_1
DELETE FROM t1 WHERE f1 > 2;


#
# Expect the 't' hits the given $sync_point_name
# which causes the server to crash.
#Expect the cluster to shrink.
#
SET SESSION wsrep_sync_wait = 0;
--let $wait_condition = SELECT VARIABLE_VALUE = 1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'wsrep_cluster_size';
--source include/wait_condition.inc
SET SESSION wsrep_sync_wait = DEFAULT;


#
# Restart the crashed node
#
--connection conn_restart
--let $galera_wsrep_recover_server_id=2
--source suite/galera/include/galera_wsrep_recover.inc

--let $_expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.2.expect
--source include/start_mysqld.inc


#
# Transaction 't' is still in PREPAREd state
# and can be committed
#
--connection node_2
--enable_reconnect
--source include/wait_until_connected_again.inc
--disable_reconnect
XA RECOVER;
XA COMMIT 't';


#
# Cleanup
#
--connection node_1
DROP TABLE t1;
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=false';

--connection node_2
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=false';

call mtr.add_suppression('Found 1 prepared XA transactions');
call mtr.add_suppression('Discovered discontinuity in recovered wsrep transaction XIDs.');


--disconnect trx
--disconnect conn_restart
