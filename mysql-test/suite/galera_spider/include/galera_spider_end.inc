--disable_result_log
--disable_query_log

if (!$galera_shards)
{
  --die ERROR IN TEST: $galera_shards must be set before sourcing include/galera_spider_end.inc
}

if (!$galera_shard_size)
{
  --die ERROR IN TEST: $galera_shard_size must be set before sourcing include/galera_spider_end.inc
}

# disconnect galera
--let $_s = $shards
while ($_s)
{
  --let $_n = $galera_shard_size
  while($_n)
  {
    --let $_conn = `select concat('node_', '$_s', '_', '$_n')`
    --disconnect $_conn
    --dec $n
  }
  --dec $s
}

# disconnect spider
--let $n = $galera_shard_size
while ($n)
{
    --connection spider_$n
    --source include/remove_spider.inc
    --disconnect spider_$n

    --dec $n
}

--enable_query_log
--enable_result_log
