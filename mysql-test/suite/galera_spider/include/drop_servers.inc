--disable_result_log
--disable_query_log

--let $__s= $galera_shards
while($__s)
{
  --let $__n= $galera_shard_size
  while($__n)
  {
    --let $__node_id = `select ($__s - 1)*$galera_shard_size + $__n`
    --let $__port= \$NODE_MYPORT_$__node_id
    --let $__server= `select concat('node_', '$__s', '_', '$__n')`
    eval DROP SERVER $__server;

    --dec $__n
  }
  --dec $__s
}

--enable_query_log
--enable_result_log
