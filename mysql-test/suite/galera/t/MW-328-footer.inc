#
# Cleanup for MW-328 tests
# 

--connection node_1
--disable_query_log

# kill may fail, if victim is in replicating phase
--error 0, ER_KILL_DENIED_ERROR
--eval KILL CONNECTION $sp_connection_id
--enable_query_log

--connection node_1X
--error 2013,1317
--reap

--connection node_1
DROP PROCEDURE proc_update;
DROP TABLE t1, t2;

CALL mtr.add_suppression("conflict state ABORTED after post commit");
