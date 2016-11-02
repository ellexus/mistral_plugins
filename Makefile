.PHONY: all
all: build-common build-mistral_mysql build-mistral_influxdb

.PHONY: build-common
build-common:
	$(MAKE) -C common

.PHONY: build-mistral_mysql
build-mistral_mysql:
	$(MAKE) -C output/mistral_mysql

.PHONY: build-mistral_influxdb
build-mistral_influxdb:
	$(MAKE) -C output/mistral_influxdb

.PHONY: clean
clean:
	$(MAKE) -C common clean
	$(MAKE) -C output/mistral_mysql clean
	$(MAKE) -C output/mistral_influxdb clean
