Mistral Elasticsearch plug-in
=============================

This plug-in receives violation data from Mistral and enters it into an
Elasticsearch Index.

Index Mappings
--------------

Prior to using the Mistral Elasticsearch plug-in the index datatype mapping
template should be configured within Elasticsearch. The file ``mappings.json``
contains the appropriate configuration. The provided template only ensures that
date fields are correctly identified other, site specific, configuration can be
added at the user's discretion.

If you have access to ``curl`` the file ``mistral_create_elastic_template.sh``
can be run to create the template from the command line.

The script takes the following command line options:

--help | -?
  Display usage instructions

--cert-path=certificate_path | -c certificate_path
  The full path to a CA certificate used to sign the certificate of the Elasticsearch server.
  See ``man openssl verify`` for details of the ``CAfile`` option.

--cert-dir=certificate_directory
  The directory that contains the CA certificate(s) used to sign the certificate of the
  Elasticsearch server. Certificates in this directory should be named after the hashed
  certificate subject name, see ``man openssl verify`` for details of the ``CApath`` option.

--date | -d
  Use date based index names e.g. ``<idx_name>-yyyy-MM-dd`` rather than the default
  of numeric indexes ``<idx_name>-0000N``.

--index=idx_name | -i idx_name
  The basename of the index. If not specified the template will be created for
  indexes called "mistral". As long as a matching option is provided to the
  plug-in it will create indexes named ``<idx_name>-yyyy-MM-dd``.

--host=hostname | -h hostname
  The name of the machine on which Elasticsearch is hosted. If not specified the
  script will use "localhost".

--password=filename | -p filename
  The name of a file containing the password to be used when creating the index.
  The password must be on a single line.

--port=n | -P n
  The port to be used when connecting to Elasticsearch. If not specified the
  script will use port 9200.

--skip-ssl-validation | -k
  Disable SSL certificate validation when connecting to Elasticsearch.

--ssl | -s
  Use HTTPS protocol instead of HTTP to connect to Elasticsearch.

--username=user | -u user
  The username to be used when connecting to Elasticsearch.


Plug-in Configuration
---------------------

The plug-in accepts the following command line options:

--index=idx_name | -i idx_name
  The basename of the index. If not specified the index will be called
  "mistral". This must match the value used when creating the index template.
  The plug-in will create indexes named ``<idx_name>-yyyy-MM-dd``.

--error=filename | -e filename
  The name of the file to which any error messages will be written.

--host=hostname | -h hostname
  The name of the machine on which Elasticsearch is hosted. If not specified the
  plug-in will use "localhost".

--mode=octal-mode | -m octal-mode
  Permissions used to create the error log file specified by the -e option.

--password=secret | -p secret
  The password to be used when accessing the index.

--port=number | -P number
  The port to be used when accessing the index. If not specified the plug-in
  will use port 9200.

--ssl | -s
  Use HTTPS protocol instead of HTTP when accessing the index.

--username=user | -u user
  The username to be used when accessing the database.

--var=var-name | -v var-name
  The name of an environment variable, the value of which should be stored by
  the plug-in. This option can be specified multiple times.

The options would normally be included in a plug-in configuration file, such as

::

   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_elasticsearch

   INTERVAL,5

   PLUGIN_OPTION,--index=mistral
   PLUGIN_OPTION,--host=10.33.0.186
   PLUGIN_OPTION,--port=9200
   PLUGIN_OPTION,--username=myname
   PLUGIN_OPTION,--password=secret
   PLUGIN_OPTION,--var=USER
   PLUGIN_OPTION,--var=SHELL
   PLUGIN_OPTION,--error=/path/to/mistral_elasticsearch.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.
