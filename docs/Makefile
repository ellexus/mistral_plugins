# The authoritative list of documents. If you find yourself putting
# them somewhere else, work out how to get them from here instead
# (e.g. with the echo-all target below).

DOCS = \
    mistral_elasticsearch_plugin_manual \
    mistral_influxdb_plugin_manual \
    mistral_splunk_plugin_manual \
    mistral_graphite_plugin_manual \
    mistral_mysql_plugin_manual \
    mistral_plugin_api

# Pandoc.
PANDOC_VERSION = $(shell which pandoc >/dev/null && pandoc --version | grep ^pandoc | sed 's/^.* //g' | cut -f1 -d.)

# Pandoc options for both HTML and PDF.
COMMON_OPTIONS = -f markdown -N -s --toc

# Pandoc options for HTML.

# Generated HTML includes this explicit filename for CSS. It doesn't
# have to exist to generate the HTML.
CSS_FILE = ../healthcheck-resources/document.css

HTML_OPTIONS = -t html -c $(CSS_FILE)

# Pandoc options for PDF.

# pdf-engine argument replaced latex-engine in version 2.
ENGINE = --pdf-engine=xelatex
ifeq "$(PANDOC_VERSION)" "1"
	ENGINE = --latex-engine=xelatex
endif

PDF_OPTIONS = $(ENGINE) -V mainfont='Liberation Sans' -V monofont='Liberation Mono' \
		-V geometry:margin=1in -V papersize:a4 -V colorlinks -V graphics \
		--include-in-header=ellexus.latex --include-before-body titlepage.tex

# If running on a remote machine, the version file needs to be in this
# directory so that remote-compile will copy it across. So
# create-docs.sh copies it in here. So if running make locally, it's
# not present so we make a symlink to it.

$(shell if [ ! -f version ]; then ln -s ../configuration/VERSION version; fi)

PDFS =  $(foreach DOC, $(DOCS), $(DOC).pdf)
HTMLS = $(foreach DOC, $(DOCS), $(DOC).html)

all: html pdf

pdf: $(PDFS)

html: $(HTMLS)

# This rule allows us to list all the doc targets, for scripts.
echo-all:
	@echo $(PDFS) $(HTMLS)

clean:
	rm -rf $(PDFS) $(HTMLS)

%.pdf: %.md
	grep "\title" $< | head -1 | sed 's/\% //' > title
	pandoc -o $@ $(PDF_OPTIONS) $(COMMON_OPTIONS) $<
	rm -f title

%.html: %.md
	pandoc -o $@ $(HTML_OPTIONS) $(COMMON_OPTIONS) $<
