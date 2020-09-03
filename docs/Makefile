# The authoritative list of documents. If you find yourself putting
# them somewhere else, work out how to get them from here instead
# (e.g. with the echo-all target below).

DOCS = \
    mistral_elasticsearch_plugin_manual \
    mistral_graphite_plugin_manual \
    mistral_influxdb_plugin_manual \
    mistral_mysql_plugin_manual \
    mistral_plugin_api \
    mistral_splunk_plugin_manual

# Pandoc.
PANDOC_VERSION = $(shell which pandoc >/dev/null && pandoc --version | grep ^pandoc | sed 's/^.* //g' | cut -f1 -d.)

# Pandoc options for both HTML and PDF.
COMMON_OPTIONS = -f markdown -N -s --toc

# Pandoc options for PDF.

# pdf-engine argument replaced latex-engine in version 2.
ENGINE = --pdf-engine=xelatex
ifeq "$(PANDOC_VERSION)" "1"
	ENGINE = --latex-engine=xelatex
endif

PDF_OPTIONS = $(ENGINE) -V mainfont='Liberation Sans' -V monofont='Liberation Mono' \
		-V geometry:margin=1in -V papersize:a4 -V colorlinks -V graphics \
		--include-in-header=ellexus.latex --include-before-body titlepage.tex

PDFS =  $(foreach DOC, $(DOCS), $(DOC).pdf)

all: pdf

pdf: $(PDFS)

# This rule allows us to list all the doc targets, for scripts.
echo-all:
	@echo $(PDFS)

clean:
	rm -rf $(PDFS)

%.pdf: %.md
	grep "\title" $< | head -1 | sed 's/\% //' > title
	pandoc -o $@ $(PDF_OPTIONS) $(COMMON_OPTIONS) $<
	rm -f title
