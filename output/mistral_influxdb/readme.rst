Mistral Influxdb plugin
=======================

This plugin receives violation data from Mistral and enters it into an Influxdb
database.

The plugin accepts the following command line options:

-d, --database
  The name of the database. If not specified the database will be called
  "mistral".

-e, --error
  The name of the file to which any error messages will be written.

-h, --host
  The name of the machine on which the database is hosted. If not specified the
  plugin will use "localhost

-p, --password
  The password to be used when accessing the database.

-P, --port
  The port to be used when accessing the database. If not specified the plugin
  will use port 8086.

-s, --https
  The protocol to be used when accessing the database. If not specified the
  plugin will use "http".

-u, --username
  The username to be used when accessing the database.

The options would normally be included in a plugin configuration file, such as

::
   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_influxdb.x86_64

   INTERVAL,5

   PLUGIN_OPTION,--database
   PLUGIN_OPTION,mistral
   PLUGIN_OPTION,--host
   PLUGIN_OPTION,10.33.0.186
   PLUGIN_OPTION,--port
   PLUGIN_OPTION,8086
   PLUGIN_OPTION,--username
   PLUGIN_OPTION,myname
   PLUGIN_OPTION,--password
   PLUGIN_OPTION,secret
   PLUGIN_OPTION,--error
   PLUGIN_OPTION,/path/to/mistral_influxdb.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plugin configuration file.
