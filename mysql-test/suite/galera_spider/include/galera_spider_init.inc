# start galera shards

--source include/have_wsrep_enabled.inc
--source include/have_partition.inc

if (!$galera_shards)
{
  --die ERROR IN TEST: $galera_shards must be set before sourcing include/galera_spider_init.inc
}

if (!$galera_shard_size)
{
  --die ERROR IN TEST: $galera_shard_size must be set before sourcing include/galera_spider_init.inc
}

# start galera shards
# ------------------------

--let $_s= $galera_shards
while($_s)
{
  --let $_n= $galera_shard_size
  while($_n)
  {
    --let $_node_id = `select ($_s - 1)*$galera_shard_size + $_n`
    --let $_port= \$NODE_MYPORT_$_node_id
    --let $_conn = `select concat('node_', '$_s', '_', '$_n')`
    --disable_query_log
    --connect($_conn,127.0.0.1,root,,test,$_port,)
    --enable_query_log
    --source include/galera_wait_ready.inc

    --dec $_n
  }
  --dec $_s
}

# start $galera_shard_size spider nodes
# ----------------------------------------

--let $_n = $galera_shard_size
while($_n)
{
  --let $_node_id= `select $galera_shard_size*$galera_shards + $_n`
  --let $_port= \$NODE_MYPORT_$_node_id
  --disable_query_log
  --connect(spider_$_n,127.0.0.1,root,,test,$_port,)
  --enable_query_log
  --source include/install_spider.inc
  --source include/have_spider.inc
  --source include/create_servers.inc

  --dec $_n
}

--disable_query_log
--connection node_1_1
--enable_query_log
