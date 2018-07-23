#include <string.h>
#include <stdio.h>
#include <time.h>
#include "utils.h"
#include "crypto.h"
#include "ocl.h"
#include "dsi.h"
#include "ocl_brute.h"

typedef int s32;

// 123 -> 0x123
static cl_ulong to_bcd(cl_ulong i) {
	cl_ulong o = 0;
	unsigned shift = 0;
	while (i) {
		o |= (i % 10) << (shift++ << 2);
		i /= 10;
	}
	return o;
}

// 0x123 -> 123
static cl_ulong from_bcd(cl_ulong i) {
	cl_ulong o = 0;
	cl_ulong pow = 1;
	while (i) {
		o += (i & 0xf) * pow;
		i >>= 4;
		pow *= 10;
	}
	return o;
}

int ocl_brute_console_id(const cl_uchar *console_id, const cl_uchar *emmc_cid,
	cl_uint offset0, const cl_uchar *src0, const cl_uchar *ver0,
	cl_uint offset1, const cl_uchar *src1, const cl_uchar *ver1,
	ocl_brute_mode mode)
{
	// preparing args
	cl_ulong console_id_template = u64be(console_id);
	cl_ulong xor0[2] = { 0 }, xor1[2] = { 0 };
	dsi_make_xor((u8*)xor0, src0, ver0);
	if (src1 != 0) {
		dsi_make_xor((u8*)xor1, src1, ver1);
	}
	cl_uint ctr[4] = { 0 };
	if (emmc_cid != 0) {
		dsi_make_ctr((u8*)ctr, emmc_cid, offset0);
	}
	cl_ulong out = 0;
#if DEBUG
	{
		printf("XOR     : %s\n", hexdump(xor0, 16, 0));
		u8 aes_key[16];
		dsi_make_key(aes_key, u64be(console_id));
		printf("AES KEY : %s\n", hexdump(aes_key, 16, 0));
		aes_init();
		aes_set_key_enc_128(aes_key);
		printf("CTR     : %s\n", hexdump(ctr, 16, 0));
		aes_encrypt_128((u8*)ctr, (u8*)xor0);
		printf("XOR TRY : %s\n", hexdump(xor0, 16, 0));
		// exit(1);
	}
#endif

	TimeHP t0, t1; long long td = 0;

	cl_int err;
	cl_platform_id platform_id;
	cl_device_id device_id;
	ocl_get_device(&platform_id, &device_id);
	if (platform_id == NULL || device_id == NULL) {
		return -1;
	}

	cl_context context = OCL_ASSERT2(clCreateContext(0, 1, &device_id, NULL, NULL, &err));
	cl_command_queue command_queue = OCL_ASSERT2(clCreateCommandQueue(context, device_id, 0, &err));

	const char *source_names[] = {
		"cl/common.h",
		"cl/aes_tables.cl",
		"cl/aes_128.cl",
		"cl/dsi.h",
		"cl/bcd.h",
		emmc_cid != 0 ? "cl/kernel_console_id.cl" : "cl/kernel_console_id_ds3.cl" };
	const char * options;
	if (mode == NORMAL) {
		options = "-w -Werror";
	} else if (mode == BCD) {
		options = "-w -Werror -DBCD";
	} else if (mode == CTR) {
		options = "-w -Werror -DCTR";
	} else {
		return -1;
	}
	cl_program program = ocl_build_from_sources(sizeof(source_names) / sizeof(char *),
		source_names, context, device_id, options);

	cl_kernel kernel = OCL_ASSERT2(clCreateKernel(program, emmc_cid != 0 ? "test_console_id" : "test_console_id_ds3", &err));

	size_t local;
	OCL_ASSERT(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));
	printf("local work size: %u\n", (unsigned)local);

	// there's no option to create it zero initialized
	cl_mem mem_out = OCL_ASSERT2(clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_ulong), NULL, &err));
	OCL_ASSERT(clEnqueueWriteBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_ulong), &out, 0, NULL, NULL));

	unsigned template_bits;
	cl_ulong total;
	unsigned group_bits;
	size_t num_items;
	unsigned loops;
	if (mode == NORMAL || mode == CTR) {
		// CTR mode is actually quite similar to NORMAL except we brute the lower 31 bits
		// since bit 31 doesn't matter, got ORed with 1 when making key
		template_bits = 32;
		total = 1ull << (64 - template_bits);
		group_bits = 28;
		num_items = 1ull << group_bits; // 268435456
		loops = 1 << (64 - template_bits - group_bits);
		if (mode == CTR) {
			loops >>= 1;
		}
	} else {
		// 0x08a15????????1?? as example, 10 digit to brute, beware the known digit near the end
		// 5 BCD digits, used to calculate template mask
		template_bits = 20;
		// I wish we could use 1e10 in C, counting 0 is not good to your eye
		total = from_bcd(1ull << 40);
		// work items variations on lower bits per enqueue, 8 + 1 digits, including the known digit
		// reduced from 36 to 28 to make nvidia runtime happy
		group_bits = 28;
		// work items per enqueue, don't count the known digit here
		num_items = from_bcd(1ull << (group_bits - 4));
		// between the template bits and group bits, it's the loop bits
		loops = from_bcd(1 << (64 - template_bits - group_bits));
	}
	// it needs to be aligned, a little overhead hurts nobody
	if (num_items % local) {
		// printf("alignment: %"LL"d -> ", num_items);
		num_items = (num_items / local + 1) * local;
		// printf("%"LL"d\n", num_items);
	}
	console_id_template &= ((cl_ulong)-1ll) << (64 - template_bits);
	// printf("total: %I64u, 0x%I64x\n", total, total);
	// printf("num_items: %I64u, 0x%I64x\n", num_items, num_items);
	// printf("steps: %u, 0x%x\n", steps, steps);
	OCL_ASSERT(clSetKernelArg(kernel, 7, sizeof(cl_mem), &mem_out));
	if (emmc_cid != 0) {
		OCL_ASSERT(clSetKernelArg(kernel, 1, sizeof(cl_uint), &ctr[0]));
		OCL_ASSERT(clSetKernelArg(kernel, 2, sizeof(cl_uint), &ctr[1]));
		OCL_ASSERT(clSetKernelArg(kernel, 3, sizeof(cl_uint), &ctr[2]));
		OCL_ASSERT(clSetKernelArg(kernel, 4, sizeof(cl_uint), &ctr[3]));
		OCL_ASSERT(clSetKernelArg(kernel, 5, sizeof(cl_ulong), &xor0[0]));
		OCL_ASSERT(clSetKernelArg(kernel, 6, sizeof(cl_ulong), &xor0[1]));
	} else {
		OCL_ASSERT(clSetKernelArg(kernel, 1, sizeof(cl_uint), &offset0));
		OCL_ASSERT(clSetKernelArg(kernel, 2, sizeof(cl_ulong), &xor0[0]));
		OCL_ASSERT(clSetKernelArg(kernel, 3, sizeof(cl_ulong), &xor0[1]));
		OCL_ASSERT(clSetKernelArg(kernel, 4, sizeof(cl_uint), &offset1));
		OCL_ASSERT(clSetKernelArg(kernel, 5, sizeof(cl_ulong), &xor1[0]));
		OCL_ASSERT(clSetKernelArg(kernel, 6, sizeof(cl_ulong), &xor1[1]));
	}
	get_hp_time(&t0);
	for (unsigned i = 0; i < loops; ++i) {
		cl_ulong console_id = console_id_template;
		if (mode == BCD) {
			console_id |= to_bcd(i) << group_bits;
		} else {
			console_id |= (u64)i << group_bits;
		}
		printf("%016"LL"x\n", (unsigned long long) console_id);
		OCL_ASSERT(clSetKernelArg(kernel, 0, sizeof(cl_ulong), &console_id));

		OCL_ASSERT(clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &num_items, &local, 0, NULL, NULL));
		clFinish(command_queue);

		OCL_ASSERT(clEnqueueReadBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_ulong), &out, 0, NULL, NULL));
		if (out) {
			get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
			printf("got a hit: %016"LL"x\n", (unsigned long long) out);
			// also write to a file
			dump_to_file(emmc_cid ? hexdump(emmc_cid, 16, 0) : hexdump(src0, 16, 0), &out, 8);
			break;
		}
	}

	u64 tested = 0;
	if (!out) {
		tested = total;
		get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
	} else {
		tested = out - console_id_template;
		if (mode == BCD) {
			tested = from_bcd(((tested & ~0xfff) >> 4) | (tested & 0xff));
		}
	}
	printf("%.2f seconds, %.2f M/s\n", td / 1000000.0, tested * 1.0 / td);

	clReleaseKernel(kernel);
	clReleaseMemObject(mem_out);
	clReleaseProgram(program);
	clReleaseCommandQueue(command_queue);
	clReleaseContext(context);
	return !out;
}

int ocl_brute_emmc_cid(const cl_uchar *console_id, cl_uchar *emmc_cid,
	cl_uint offset, const cl_uchar *src, const cl_uchar *ver)
{
	// preparing args
	u8 aes_key[16];
	dsi_make_key(aes_key, u64be(console_id));
	aes_init();
	aes_set_key_dec_128(aes_key);
	cl_ulong xor[2];
	dsi_make_xor((u8*)xor, src, ver);
	cl_ulong ctr[2];
	aes_decrypt_128((u8*)xor , (u8*)ctr);
	cl_ulong emmc_cid_sha1_16[2];
	byte_reverse_16((u8*)emmc_cid_sha1_16, (u8*)ctr);
	sub_128_64(emmc_cid_sha1_16, offset);
	cl_ulong out = 0;
#ifdef DEBUG
	{
		printf("SHA1 A: %s\n", hexdump(emmc_cid_sha1_16, 16, 0));
		u8 sha1_verify[16];
		sha1_16(emmc_cid, sha1_verify);
		printf("SHA1 B: %s\n", hexdump(sha1_verify, 16, 0));
	}
#endif

	TimeHP t0, t1; long long td = 0;

	cl_int err;
	cl_platform_id platform_id;
	cl_device_id device_id;
	ocl_get_device(&platform_id, &device_id);
	if (platform_id == NULL || device_id == NULL) {
		return -1;
	}

	cl_context context = OCL_ASSERT2(clCreateContext(0, 1, &device_id, NULL, NULL, &err));
	cl_command_queue command_queue = OCL_ASSERT2(clCreateCommandQueue(context, device_id, 0, &err));

	const char *source_names[] = {
		"cl/common.h",
		"cl/sha1_16.cl",
		"cl/kernel_emmc_cid.cl" };
	cl_program program = ocl_build_from_sources(sizeof(source_names) / sizeof(char *),
		source_names, context, device_id, "-w -Werror");

	cl_kernel kernel = OCL_ASSERT2(clCreateKernel(program, "test_emmc_cid", &err));

	size_t local;
	OCL_ASSERT(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));
	printf("local work size: %u\n", (unsigned)local);

	// there's no option to create it zero initialized
	cl_mem mem_out = OCL_ASSERT2(clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_ulong), NULL, &err));
	OCL_ASSERT(clEnqueueWriteBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_ulong), &out, 0, NULL, NULL));

	unsigned brute_bits = 32;
	unsigned group_bits = 28;
	unsigned loop_bits = brute_bits - group_bits;
	unsigned loops = 1ull << loop_bits;
	size_t num_items = 1ull << group_bits;
	// it needs to be aligned, a little overhead hurts nobody
	if (num_items % local) {
		num_items = (num_items / local + 1) * local;
	}
	OCL_ASSERT(clSetKernelArg(kernel, 2, sizeof(cl_ulong), emmc_cid_sha1_16));
	OCL_ASSERT(clSetKernelArg(kernel, 3, sizeof(cl_ulong), emmc_cid_sha1_16 + 1));
	OCL_ASSERT(clSetKernelArg(kernel, 4, sizeof(cl_mem), &mem_out));
	get_hp_time(&t0);
	for (unsigned i = 0; i < loops; ++i) {
		*(u32*)(emmc_cid + 1) = i << group_bits;
		puts(hexdump(emmc_cid, 16, 0));
		OCL_ASSERT(clSetKernelArg(kernel, 0, sizeof(cl_ulong), emmc_cid));
		OCL_ASSERT(clSetKernelArg(kernel, 1, sizeof(cl_ulong), emmc_cid + 8));

		OCL_ASSERT(clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &num_items, &local, 0, NULL, NULL));
		clFinish(command_queue);

		OCL_ASSERT(clEnqueueReadBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_ulong), &out, 0, NULL, NULL));
		if (out) {
			get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
			*(u32*)(emmc_cid + 1) |= out;
			printf("got a hit: %s\n", hexdump(emmc_cid, 16, 0));
			// also write to a file
			dump_to_file(hexdump(console_id, 8, 0), emmc_cid, 16);
			break;
		}
	}

	u64 tested = 0;
	if (!out) {
		tested = 1ull << 32;
		get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
	} else {
		tested = *(u32*)(emmc_cid + 1);
	}
	printf("%.2f seconds, %.2f M/s\n", td / 1000000.0, tested * 1.0 / td);

	clReleaseKernel(kernel);
	clReleaseMemObject(mem_out);
	clReleaseProgram(program);
	clReleaseCommandQueue(command_queue);
	clReleaseContext(context);
	return !out;
}

/* more info:
 * https://zoogie.github.io/web/34%E2%85%95c3/#/22
 * https://gbatemp.net/threads/eol-is-lol-the-34c3-talk-for-3ds-that-never-was.494698/
 * what I'm doing here is simply brute the 3rd u32 of a u128 so that the first half of sha256 matches ver
 */
int ocl_brute_msky(const cl_uint *msky, const cl_uint *ver, cl_uint msky_offset, cl_uint msky_max_offset)
{
	TimeHP t0, t1; long long td = 0;

	cl_int err;
	cl_platform_id platform_id;
	cl_device_id device_id;
	ocl_get_device(&platform_id, &device_id);
	if (platform_id == NULL || device_id == NULL) {
		return -1;
	}

	cl_context context = OCL_ASSERT2(clCreateContext(0, 1, &device_id, NULL, NULL, &err));
	cl_command_queue command_queue = OCL_ASSERT2(clCreateCommandQueue(context, device_id, 0, &err));

	const char *source_names[] = {
		"cl/common.h",
		"cl/sha256_16.cl",
		"cl/kernel_msky.cl" };
	cl_program program = ocl_build_from_sources(sizeof(source_names) / sizeof(char *),
		source_names, context, device_id, "-w -Werror -DSHA256_16");

	cl_kernel kernel = OCL_ASSERT2(clCreateKernel(program, "test_msky", &err));

	size_t local;
	OCL_ASSERT(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));
	if (seedminer_mode != 1 || reduced_work_size_mode != 1) {
		printf("local work size: %u\n", (unsigned)local);
	}

	// there's no option to create it zero initialized
	cl_uint out = 0;
	cl_mem mem_out = OCL_ASSERT2(clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint), NULL, &err));
	OCL_ASSERT(clEnqueueWriteBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_uint), &out, 0, NULL, NULL));

	unsigned brute_bits = 32;
	unsigned group_bits = 28;
	if (reduced_work_size_mode == 1) {
		group_bits = 20;
	}
	unsigned loop_bits = brute_bits - group_bits;
	unsigned loops = 1ull << loop_bits;
	size_t num_items = 1ull << group_bits;
	// it needs to be aligned, a little overhead hurts nobody
	if (num_items % local) {
		num_items = (num_items / local + 1) * local;
	}
	OCL_ASSERT(clSetKernelArg(kernel, 0, sizeof(cl_uint), &msky[0]));
	OCL_ASSERT(clSetKernelArg(kernel, 1, sizeof(cl_uint), &msky[1]));
	OCL_ASSERT(clSetKernelArg(kernel, 4, sizeof(cl_uint), &ver[0]));
	OCL_ASSERT(clSetKernelArg(kernel, 5, sizeof(cl_uint), &ver[1]));
	OCL_ASSERT(clSetKernelArg(kernel, 6, sizeof(cl_uint), &ver[2]));
	OCL_ASSERT(clSetKernelArg(kernel, 7, sizeof(cl_uint), &ver[3]));
	OCL_ASSERT(clSetKernelArg(kernel, 8, sizeof(cl_mem), &mem_out));
	get_hp_time(&t0);
	int msky3_range = msky_max_offset; // You should in theory, at the most, "fan out" +/-8192 on msky3; that being said, an msky_max_offset is required from the user
	unsigned i, j, k=0;
	for (j = msky_offset; j < msky3_range; ++j) {
		int msky3_offset = (j & 1 ? 1 : -1) * ((j + 1) >> 1);
		cl_uint msky3 = msky[3] + msky3_offset;
		printf("msed3:%08x offset:%d    \r", msky3, msky3_offset);
		fflush(stdout);
		for (i = 0; i < loops; ++i) {
			cl_uint msky2 = i << group_bits;
			OCL_ASSERT(clSetKernelArg(kernel, 2, sizeof(cl_uint), &msky2));
			OCL_ASSERT(clSetKernelArg(kernel, 3, sizeof(cl_uint), &msky3));

			OCL_ASSERT(clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &num_items, &local, 0, NULL, NULL));
			clFinish(command_queue);

			OCL_ASSERT(clEnqueueReadBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_uint), &out, 0, NULL, NULL));
			if (out) {
				get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
				cl_uint msky_ret[4] = { msky[0], msky[1], out, msky3 };
				printf("got a hit: %s at offset: %d\n", hexdump(msky_ret, 16, 0), msky3_offset);
				u8 msed[0x140]={0};
				memcpy(&msed[0x110], msky_ret, 16);
				dump_to_file("movable.sed", msed, 0x140);
				printf("movable.sed dumped to file!\n");
				struct msed_data{
					u32 lfcs_blk;
					s32 msed3offset;
					u32 seedtype;
				} mdata;
				srand(time(NULL));
				u32 rnd=rand();
				char filename[0x100]={0};
				u32 lfcs=msky[0];
				u32 lfcs_blk=lfcs >> 12;
				u32 msed3=msky3;
				s32 msed3offset=(lfcs/5)-(msed3&0x7FFFFFFF);
				u32 seedtype=(msed3&0x80000000)>>31;
				mdata.lfcs_blk=lfcs_blk;
				mdata.msed3offset=msed3offset;
				mdata.seedtype=seedtype;
				snprintf(filename, 0x100, "msed_data_%08X.bin", rnd);
				printf("msed_data will also be written to\n%s\n",filename);
				printf("just keep it handy if you don't know what to do with it!\n\n");
				dump_to_file(filename, &mdata, 12);
				printf("done.\n");
				break;
			}
		}
		if (out) {
			break;
		}
		k++;
	}
	u64 tested = 0;
	if (!out) {
		tested = (1ull << brute_bits) * k;
		get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
	} else {
		tested = out + (1ull << brute_bits) * k;
	}
	printf("%.2f seconds, %.2f M/s\n", td / 1000000.0, tested * 1.0 / td);
	clReleaseKernel(kernel);
	clReleaseMemObject(mem_out);
	clReleaseProgram(program);
	clReleaseCommandQueue(command_queue);
	clReleaseContext(context);
	if (!out) { // Could any problems happen because of this?
		printf("Max offset reached! Brute-forcing will now terminate!\n");
		exit(101); // For lack of a better exit code
	} else {
		return !out;
	}
}

// LFCS brute force, https://gist.github.com/zoogie/4046726878dba89eddfa1fc07c8a27da
int ocl_brute_lfcs(cl_uint lfcs_template, cl_ushort newflag, const cl_uint *ver, cl_uint lfcs_offset)
{
	TimeHP t0, t1; long long td = 0;

	cl_int err;
	cl_platform_id platform_id;
	cl_device_id device_id;
	ocl_get_device(&platform_id, &device_id);
	if (platform_id == NULL || device_id == NULL) {
		return -1;
	}

	cl_context context = OCL_ASSERT2(clCreateContext(0, 1, &device_id, NULL, NULL, &err));
	cl_command_queue command_queue = OCL_ASSERT2(clCreateCommandQueue(context, device_id, 0, &err));

	const char *source_names[] = {
		"cl/common.h",
		"cl/sha256_16.cl",
		"cl/kernel_lfcs.cl" };
	cl_program program = ocl_build_from_sources(sizeof(source_names) / sizeof(char *),
		source_names, context, device_id, "-w -Werror -DSHA256_12");

	cl_kernel kernel = OCL_ASSERT2(clCreateKernel(program, "test_lfcs", &err));

	size_t local;
	OCL_ASSERT(clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL));
	if (seedminer_mode != 1 || reduced_work_size_mode != 1) {
		printf("local work size: %u\n", (unsigned)local);
	}

	// there's no option to create it zero initialized
	cl_uint out = 0;
	cl_mem mem_out = OCL_ASSERT2(clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(cl_uint), NULL, &err));
	OCL_ASSERT(clEnqueueWriteBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_uint), &out, 0, NULL, NULL));

	unsigned brute_bits = 32;
	unsigned group_bits = 28;
	if (reduced_work_size_mode == 1) {
		group_bits = 20;
	}
	unsigned loop_bits = brute_bits - group_bits;
	unsigned loops = 1ull << loop_bits;
	size_t num_items = 1ull << group_bits;
	// it needs to be aligned, a little overhead hurts nobody
	if (num_items % local) {
		num_items = (num_items / local + 1) * local;
	}
	OCL_ASSERT(clSetKernelArg(kernel, 1, sizeof(cl_ushort), &newflag));
	OCL_ASSERT(clSetKernelArg(kernel, 2, sizeof(cl_uint), &ver[0]));
	OCL_ASSERT(clSetKernelArg(kernel, 3, sizeof(cl_uint), &ver[1]));
	OCL_ASSERT(clSetKernelArg(kernel, 4, sizeof(cl_mem), &mem_out));
	get_hp_time(&t0);
	int fan_range = 0x10000; // "fan out" full 16 bits
	unsigned i, j,k=0;
	int upper_bound = newflag ? 0x05000000>>16 : 0x0B000000>>16;  //these upper bounds are safe bets now, but that may
	int lower_bound = 0;                                          //change as time passes. will need to update
	//lfcs_template=0;  //debug
	u32 lfcs_block = lfcs_template>>16;
	for (j = lfcs_offset; j < fan_range; ++j) {
		int fan = (j & 1 ? 1 : -1) * ((j + 1) >> 1);

		if(fan > 0){                                         //check to see if bf exhausted in both directions, quit if so
			if( lfcs_block + fan  > upper_bound  &&  (int)lfcs_block - fan < lower_bound){
				printf("Exhausted all possible lfcs combinations, exiting ...\n\n");
				break;
			}
			if(lfcs_block + fan > upper_bound) continue;     //check to see if bf exhausted in + direction, skip iteration if so
		}
		else{
			if((int)lfcs_block + fan < lower_bound) continue;//check to see if bf exhausted in - direction, skip iteration if so
		}

		printf("offset: %d \r", fan);
		fflush(stdout);
		for (i = 0; i < loops; ++i) {
			cl_uint lfcs = lfcs_template + fan * 0x10000 + (i << (group_bits - 16));
			OCL_ASSERT(clSetKernelArg(kernel, 0, sizeof(cl_uint), &lfcs));

			OCL_ASSERT(clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &num_items, &local, 0, NULL, NULL));
			clFinish(command_queue);

			OCL_ASSERT(clEnqueueReadBuffer(command_queue, mem_out, CL_TRUE, 0, sizeof(cl_uint), &out, 0, NULL, NULL));
			if (out) {
				get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
				lfcs += out >> 16;
				printf("got a hit: %s (rand: 0x%04x) at offset: %d\n", hexdump(&lfcs, 4, 0), out & 0xffff, lfcs_offset);
				u8 part1[0x1000]={0};
				memcpy(part1, &lfcs, 4);
				memcpy(part1+4, &newflag, 2);
				FILE *f=fopen("movable_part1.sed","rb+");
				if(f){
					printf("existing movable_part1.sed found, adding lfcs...\n");
					fwrite(part1, 1, 8, f);
					fclose(f);
				}
				else{
					printf("movable_part1.sed not found, generating a new one...\n");
					dump_to_file("movable_part1.sed", part1, 0x1000);
					printf("movable_part1.sed dumped to file\n");
					printf("don't you dare forget to add the id0 to it!\n");
				}

				printf("done.\n\n");
				break;
			}

		}

		if (out) {
			break;
		}
		k++;
	}
	u64 tested = 0;
	if (!out) {
		tested = (1ull << brute_bits) * k;
		get_hp_time(&t1); td = hp_time_diff(&t0, &t1);
	} else {
		tested = out + (1ull << brute_bits) * k;
	}
	printf("%.2f seconds, %.2f M/s\n", td / 1000000.0, tested * 1.0 / td);

	clReleaseKernel(kernel);
	clReleaseMemObject(mem_out);
	clReleaseProgram(program);
	clReleaseCommandQueue(command_queue);
	clReleaseContext(context);
	return !out;
}
