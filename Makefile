PNAME = bfcl
OBJS = $(PNAME).o ocl_util.o utils.o sha1_16.o aes_128.o ocl_test.o ocl_brute.o
ifdef SYSTEMROOT
	# Intel's OpenCL SDK Windows installer sets an environmental variable. If it's not set, define only CFLAGS.
	ifdef INTELOCLSDKROOT
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I$(INTELOCLSDKROOT)\include
		LDFLAGS += -L$(INTELOCLSDKROOT)\lib\x64
	else
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd
	endif
else
	ifeq ($(shell uname), Linux)
		# Intel's OpenCL SDK Linux installer doesn't set an environment variable, so we'll have to specify its default installation location instead, regardless if it exists or not.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I/opt/intel/opencl-sdk/include
		LDFLAGS += -L/opt/intel/opencl-sdk/lib64
	endif
	ifeq ($(shell uname), Darwin)
		# macOS's linker likes to warn you about library dirs not being found. That being said, macOS includes its own implementation of OpenCL, so custom LDFLAGS don't need to be defined.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd
	endif
endif

all: $(PNAME)

$(PNAME): $(OBJS)
ifeq ($(shell uname), Darwin)
	$(CC) -o $@ $^ -framework OpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead (on macOS), change "-lmbedcrypto" to "/usr/local/lib/libmbedcrypto.a" (if you downloaded mbedtls through Homebrew) with the quotes.
else
	$(CC) $(LDFLAGS) -o $@ $^ -lOpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead (whether you're using MSYS2 or are on Linux), change "-lmbedcrypto" to "-l:libmbedcrypto.a" without the quotes.
# Note: Ubuntu (probably Debian as well) doesn't install "libmbedcrypto.a" through apt-get, thus you would have to compile mbedtls yourself if you want to use its static libraries.
endif

clean:
	rm -f $(PNAME) *.o