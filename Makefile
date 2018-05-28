PNAME = bfcl
OBJS = $(PNAME).o ocl_util.o utils.o sha1_16.o aes_128.o ocl_test.o ocl_brute.o
ifdef SYSTEMROOT
	# I specified the environmental variable Intel's OpenCL SDK installer sets on Windows.
	CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I$(INTELOCLSDKROOT)\include
	LDFLAGS += -L$(INTELOCLSDKROOT)\lib\x64
else
	ifeq ($(shell uname), Linux)
		# I directly specified the deafult location the installer installs to on Linux since no environment variable is set.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I/opt/intel/opencl-sdk/include
		LDFLAGS += -L/opt/intel/opencl-sdk/lib64
	endif
	ifeq ($(shell uname), Darwin)
		# macOS's "ld" likes to warn you about library dirs not found. Also, macOS includes its own implementation of OpenCL.
		CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd
	endif
endif

all: $(PNAME)

$(PNAME): $(OBJS)
ifeq ($(shell uname), Darwin)
	$(CC) $(LDFLAGS) -o $@ $^ -framework OpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead, change "-lmbedcrypto" to "/usr/local/lib/libmbedcrypto.a" with or without the quotes.
else
	$(CC) $(LDFLAGS) -o $@ $^ -lOpenCL -lmbedcrypto
# If you want to use the mbedcrypto static library instead, change "-lmbedcrypto" to "-l:libmbedcrypto.a" without the quotes.
endif

clean:
	rm -f $(PNAME) *.o
