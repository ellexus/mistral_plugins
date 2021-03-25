Mistral Splunk plug-in
======================

This plug-in receives violation data from Mistral and enters it into a Splunk
Index.

Splunk Configuration
--------------------

Prior to using the Mistral Splunk plug-in an HTTP Event Collector end point must
be configured as the related authentication token is required for correct
operation of this plug-in.


Plug-in Configuration
---------------------

The plug-in accepts the following command line options:

--cert-path=certificate_path | -c certificate_path
  The full path to a CA certificate used to sign the certificate of the Splunk server.
  See ``man openssl verify`` for details of the ``CAfile`` option.

--cert-dir=certificate_directory
  The directory that contains the CA certificate(s) used to sign the certificate of the
  Splunk server. Certificates in this directory should be named after the hashed certificate
  subject name, see ``man openssl verify`` for details of the ``CApath`` option.

--error=filename | -e filename
  The name of the file to which any error messages will be written.

--host=hostname | -h hostname
  The name of the machine on which Splunk is hosted. If not specified the
  plug-in will use "localhost".

--index=idx_name | -i idx_name
  The name of the index in which to store data, if not specified this will
  default to "main".

--mode=octal-mode | -m octal-mode
  Permissions used to create the error log file specified by the -e option.

--port=number | -p number
  Specifies the port to connect to on the Splunk server host. If not specified
  the plug-in will default to using port 8088.

--skip-ssl-validation | -k
  Disable SSL certificate validation when connecting to Splunk.

--ssl | -s
  Use HTTPS protocol instead of HTTP to connect to Splunk.

--token=hash | -t hash
  The API endpoint token required to access the Splunk server.
  If hash is specified as "file:<filename>" the plug-in will attempt to read the
  token from the first line of <filename>.

--var=var-name | -v var-name
  The name of an environment variable, the value of which should be stored by
  the plug-in. This option can be specified multiple times.

The options would normally be included in a plug-in configuration file, such as

::

   PLUGIN,OUTPUT

   PLUGIN_PATH,/path/to/mistral_splunk

   INTERVAL,5

   PLUGIN_OPTION,--index=mistral
   PLUGIN_OPTION,--host=10.33.0.186
   PLUGIN_OPTION,--port=8765
   PLUGIN_OPTION,--token=file:/home/user/secret/HEC.token
   PLUGIN_OPTION,--var=USER
   PLUGIN_OPTION,--var=SHELL
   PLUGIN_OPTION,--error=/path/to/mistral_splunk.log

   END


To enable the output plug-in you should set the ``MISTRAL_PLUGIN_CONFIG``
environment variable to point at the plug-in configuration file.
