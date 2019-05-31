
# DATA node tables, create once in each shard

--let $_s= $galera_shards
while($_s)
{
  --let $_conn = `select concat('node_', $_s, '_1')`
  --connection $_conn
  CREATE TABLE hashpart (id int(10) unsigned NOT NULL AUTO_INCREMENT,
                         val varchar(50) NOT NULL,
                         PRIMARY KEY(id));
  CREATE TABLE rangepart (id int(10) unsigned NOT NULL AUTO_INCREMENT,
                          val varchar(50) NOT NULL,
                          PRIMARY KEY(id, val));
  --dec $_s
}


# SPIDER node tables
# TODO: generate PARTITIONs from $galera_shards

--let $_n = $galera_shard_size
while($_n)
{
  --connection spider_$_n

  eval CREATE TABLE hashpart (id int(10) unsigned NOT NULL AUTO_INCREMENT,
                         val varchar(50) NOT NULL,
                         PRIMARY KEY(id))
               ENGINE=spider
               COMMENT='wrapper "mysql", table "hashpart"'
               PARTITION BY KEY (id)
               (
                   PARTITION p1 COMMENT = 'srv "node_1_$_n"',
                   PARTITION p2 COMMENT = 'srv "node_2_$_n"'
               );

  eval CREATE TABLE rangepart (id int(10) unsigned NOT NULL AUTO_INCREMENT,
                          val varchar(50) NOT NULL,
                          PRIMARY KEY(id, val))
               ENGINE=spider
               COMMENT='wrapper "mysql", table "rangepart"'
               PARTITION BY range columns (val)
               (
                   PARTITION p1 VALUES LESS THAN ('l') COMMENT = 'srv "node_1_$_n"',
                   PARTITION p2 VALUES LESS THAN (maxvalue) COMMENT = 'srv "node_2_$_n"'
               );

  --dec $_n
}

--connection node_1_1
