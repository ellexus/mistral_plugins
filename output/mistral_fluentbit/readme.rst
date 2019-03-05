Mistral Fluent Bit plug-in
==========================

This plug-in receives violation data from Mistral and sends it to Fluent Bit.

Plug-in Configuration
---------------------

The plug-in accepts the following command line options:

--error=filename | -e filename
  The name of the file to which any error messages will be written.

--host=hostname | -h hostname
  The name of the machine on which Elasticsearch is hosted. If not specified the
  plug-in will use "localhost".

--mode=octal-mode | -m octal-mode
  Permissions used to create the error log file specified by the -e option.

--port=number | -p number
  The port to be used when accessing the index. If not specified the plug-in
  will use port 5170.


The options would normally be included in a plug-in configuration file, such as

::

   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_fluentbit

   INTERVAL,1

   PLUGIN_OPTION,--host=127.0.0.1
   PLUGIN_OPTION,--port=5170
   PLUGIN_OPTION,--error=/path/to/mistral_fluentbit.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.
