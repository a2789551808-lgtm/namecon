.PHONY: build clean proto run

proto:
	./scripts/gen_proto.sh

build:
	./scripts/build.sh

run:
	./scripts/dev.sh

clean:
	rm -rf media-svc/build signal-svc/build build
