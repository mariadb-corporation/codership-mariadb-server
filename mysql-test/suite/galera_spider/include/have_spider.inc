if (`SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'spider' AND PLUGIN_STATUS='ACTIVE'`)
{
  --skip Test requires Spider plugin
}

if (!`SELECT count(*) FROM information_schema.engines WHERE
      (support = 'YES' OR support = 'DEFAULT') AND
      engine = 'Spider'`)
{
  SELECT engine, support FROM information_schema.engines;
  --let $SKIP_REASON= "Test requires Spider engine"
}
