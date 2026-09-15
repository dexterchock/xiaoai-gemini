IMAGE_NAME ?= xiaoai-plus-toolchain:dev
PROJECT_ROOT := $(abspath ..)
RUNTIME_TEST_IMAGE_NAME := xiaoai-plus-runtime-test:dev

.PHONY: build build-sherpa-static compile runtime-test-image runtime-smoke-test

build:
	docker build -t $(IMAGE_NAME) $(CURDIR)

build-sherpa-static:
	bash $(CURDIR)/build-sherpa-static.sh

compile:
	@test -f "$(PROJECT_ROOT)/assets/sherpa_onnx_static/include/sherpa-onnx/c-api/cxx-api.h" || \
		(echo "Static sherpa-onnx not found. Run: make build-sherpa-static" && exit 1)
	docker run --rm \
		-v "$(PROJECT_ROOT)":/workspace \
		-w /workspace \
		$(IMAGE_NAME) \
		bash -lc 'SYSROOT=/opt/sysroot cmake -S . -B build-armv7 \
			-DCMAKE_TOOLCHAIN_FILE=armv7-linux-gnueabihf.cmake \
			-DSHERPA_ONNX_STATIC_ROOT=/workspace/assets/sherpa_onnx_static && \
			cmake --build build-armv7 -j$$(nproc) --target xiaoai_plus_speaker'

runtime-test-image:
	docker build -t $(IMAGE_NAME) $(CURDIR)
	docker build \
		-t $(RUNTIME_TEST_IMAGE_NAME) \
		-f $(CURDIR)/runtime-test.Dockerfile \
		$(CURDIR)

runtime-smoke-test:
	bash $(CURDIR)/runtime-smoke-test.sh
