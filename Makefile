PNAME = bfcl
OBJS = $(PNAME).o ocl_util.o utils.o sha1_16.o aes_128.o ocl_test.o ocl_brute.o
ifdef SYSTEMROOT
	# Intel's OpenCL SDK installer sets an environmental variable on Windows.
	CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I$(INTELOCLSDKROOT)\include
	LDFLAGS += -L$(INTELOCLSDKROOT)\lib\x64
else
	ifeq ($(shell uname), Linux)
		# Intel's OpenCL SDK installer directly specified the default location the installer installs to on Linux since no environment variable is set.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I/opt/intel/opencl-sdk/include
		LDFLAGS += -L/opt/intel/opencl-sdk/lib64
	endif
	ifeq ($(shell uname), Darwin)
		# macOS's "ld" likes to warn you about library dirs not being found. That being said, macOS includes its own implementation of OpenCL.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd
	endif
endif

all: $(PNAME)

$(PNAME): $(OBJS)
ifeq ($(shell uname), Darwin)
	$(CC) -o $@ $^ -framework OpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead, change "-lmbedcrypto" to "/usr/local/lib/libmbedcrypto.a" (or wherever else it may be) with or without the quotes.
else
	$(CC) $(LDFLAGS) -o $@ $^ -lOpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead, change "-lmbedcrypto" to "-l:libmbedcrypto.a" without the quotes.
# Note: Ubuntu (probably Debian as well) doesn't install "libmbedcrypto.a", thus you'd have to compile mbedtls yourself.
endif

clean:
	rm -f $(PNAME) *.o
