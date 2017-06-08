Mistral Influxdb plug-in
========================

This plug-in receives violation data from Mistral and enters it into an Influxdb
database.

The plug-in accepts the following command line options:

--database=db-name
-d db-name
   Set the InfluxDB database to be used for storing data.
   Defaults to "mistral".

--error=file
-e file
   Specify location for error log. If not specified all errors will be output on
   stderr and handled by Mistral error logging.

--host=hostname
-h hostname
   The hostname of the InfluxDB server with which to establish a connection.
   If not specified the plug-in will default to "localhost".

--mode=octal-mode
-m octal-mode
   Permissions used to create the error log file specified by the -e option.

--password=secret
-p secret
   The password required to access the InfluxDB server if needed.

--port=number
-P number
   Specifies the port to connect to on the InfluxDB server host.
   If not specified the plug-in will default to "8086".

--ssl
-s
   Connect to the InfluxDB server via secure HTTP.

--username=user
-u user
   The username required to access the InfluxDB server if needed.

--var=var-name
-v var-name
   The name of an environment variable, the value of which should be stored by
   the plug-in. This option can be specified multiple times.

The options would normally be included in a plug-in configuration file, such as

::
   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_influxdb

   INTERVAL,5

   PLUGIN_OPTION,--database=mistral
   PLUGIN_OPTION,--host=10.33.0.186
   PLUGIN_OPTION,--port=8086
   PLUGIN_OPTION,--username=myname
   PLUGIN_OPTION,--password=secret
   PLUGIN_OPTION,--var=USER
   PLUGIN_OPTION,--var=SHELL
   PLUGIN_OPTION,--error=/path/to/mistral_influxdb.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.
