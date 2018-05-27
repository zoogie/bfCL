PNAME = bfcl
OBJS = $(PNAME).o ocl_util.o utils.o sha1_16.o aes_128.o ocl_test.o ocl_brute.o
ifdef SYSTEMROOT
	CFLAGS += -std=c11 -Wall -Werror -O2 -mrdrnd -I$(INTELOCLSDKROOT)\include
	LDFLAGS += -L$(INTELOCLSDKROOT)\lib\x64
else
	ifeq ($(shell uname), Linux)
		# I directly specified the deafult location the installer installs to on Linux.
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
else
	$(CC) $(LDFLAGS) -o $@ $^ -lOpenCL -lmbedcrypto
endif

clean:
	rm -f $(PNAME) *.o
