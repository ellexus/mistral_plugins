Mistral Elasticsearch plug-in
=============================

This plugin receives violation data from Mistral and enters it into an
Elasticsearch Index.

Index Mappings
--------------

Prior to using the Mistral Elasticsearch plug-in the index datatype mapping
template should be configured within Elasticsearch. The file `mappings.json`
contains the appropriate configuration. The provided template only ensures that
date fields are correctly identified other, site specific, configuration can be
added at the user's discretion.

If you have access to `curl` the file `mistral_create_elastic_template.sh` can
be run to create the template from the command line.

The script takes the following command line options:

--help
-?
  Display usage instructions

--index=idx_name
-i idx_name
  The basename of the index. If not specified the template will be created for
  indexes called "mistral". As long as a matching option is provided to the
  plug-in it will create indexes named `<idx_name>-yyyy-MM-dd`.

--host=hostname
-h hostname
  The name of the machine on which Elasticsearch is hosted. If not specified the
  script will use "localhost".

--password=filename
-p filename
  The name of a file containing the password to be used when creating the index.
  The password must be on a single line.

--port=n
-n n
  The port to be used when connecting to Elasticsearch. If not specified the
  script will use port 9200.

--ssl
-s
  Use HTTPS protocol instead of HTTP to connect to Elasticsearch.

--username=user
-u user
  The username to be used when connecting to Elasticsearch.


Plug-in Configuration
---------------------

The plugin accepts the following command line options:

--index=idx_name
-i idx_name
  The basename of the index. If not specified the index will be called
  "mistral". This must match the value used when creating the index template.
  The plug-in will create indexes named `<idx_name>-yyyy-MM-dd`.

--error=filename
-e filename
  The name of the file to which any error messages will be written.

--host=hostname
-h hostname
  The name of the machine on which Elasticsearch is hosted. If not specified the
  plug-in will use "localhost".

--mode=octal-mode
-m octal-mode
  Permissions used to create the error log file specified by the -e option.

--password=secret
-p secret
  The password to be used when accessing the index.

--port=number
-P number
  The port to be used when accessing the index. If not specified the plug-in
  will use port 9200.

--ssl
-s
  Use HTTPS protocol instead of HTTP when accessing the index.

--username=user
-u user
  The username to be used when accessing the database.

The options would normally be included in a plugin configuration file, such as

::
   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_elasticsearch

   INTERVAL,5

   PLUGIN_OPTION,--index=mistral
   PLUGIN_OPTION,--host=10.33.0.186
   PLUGIN_OPTION,--port=9200
   PLUGIN_OPTION,--username=myname
   PLUGIN_OPTION,--password=secret
   PLUGIN_OPTION,--error=/path/to/mistral_elasticsearch.log

   END


To enable the output plugin you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plugin configuration file.
