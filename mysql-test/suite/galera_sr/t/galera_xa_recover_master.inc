--connection node_1
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=true';

--connection node_2
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=true';

CREATE TABLE t1 (f1 INTEGER PRIMARY KEY) Engine=InnoDB;
INSERT INTO t1 VALUES (1);


#
# Create XA transaction 't1' and 't2' up to prepare phase
#
--connect t1, 127.0.0.1, root, , test, $NODE_MYPORT_2
--connection t1
XA START 't1';
INSERT INTO t1 VALUES (2);
INSERT INTO t1 VALUES (3);
XA END 't1';
XA PREPARE 't1';

--connect t2, 127.0.0.1, root, , test, $NODE_MYPORT_2
--connection t2
XA START 't2';
INSERT INTO t1 VALUES (4);
INSERT INTO t1 VALUES (5);
XA END 't2';
XA PREPARE 't2';


#
# Kill the node and restart it
#
--connect conn_control, 127.0.0.1, root, , test, $NODE_MYPORT_1
--connect conn_restart, 127.0.0.1, root, , test, $NODE_MYPORT_2
--let $conn_control=conn_control
--let $conn_restart=conn_restart
--let $server_id_restart=2
--source $restart_script
--disconnect conn_control
--disconnect conn_restart


#
# Trx 'test' should still be there
#
--connection node_1
--echo Expect transactions 't1' and 't2'
XA RECOVER;

--connection node_2
--enable_reconnect
--source include/wait_until_connected_again.inc
--disable_reconnect
--echo Expect transactions 't1' and 't2'
XA RECOVER;


#
# XA COMMIT after recovery should succeed
#
--connection t1
--enable_reconnect
--source include/wait_until_connected_again.inc
--disable_reconnect
XA COMMIT 't1';

--connection t2
--enable_reconnect
--source include/wait_until_connected_again.inc
--disable_reconnect
XA ROLLBACK 't2';


#
# No more traces of 'test'
#
--connection node_1
--echo Expect rows 1,2,3
SELECT * FROM t1;
--echo Expect empty set
SELECT flags, xid FROM mysql.wsrep_streaming_log;
--echo Expect empty set
XA RECOVER;

--connection node_2
--echo Expect rows 1,2,3
SELECT * FROM t1;
--echo Expect empty set
SELECT flags, xid FROM mysql.wsrep_streaming_log;
--echo Expect empty set
XA RECOVER;


#
# Cleanup
#
--connection node_1
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=false';

--connection node_2
DROP TABLE t1;
SET GLOBAL wsrep_provider_options = 'pc.ignore_sb=false';
call mtr.add_suppression('Found 2 prepared XA transactions');
call mtr.add_suppression('Discovered discontinuity in recovered wsrep transaction XIDs.');
