
# DATA node tables, create once in each shard

--let $_s= $galera_shards
while($_s)
{
  --let $_conn = `select concat('node_', $_s, '_1')`
  --connection $_conn
  DROP TABLE hashpart;
  DROP TABLE rangepart;

  --dec $_s
}


# SPIDER node tables

--let $_n = $galera_shard_size
while($_n)
{
  --connection spider_$_n
  DROP TABLE hashpart;
  DROP TABLE rangepart;

  --dec $_n
}

--connection node_1_1
