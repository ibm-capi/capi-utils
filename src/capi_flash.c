/*
 * Copyright 2016 International Business Machines
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE /* For asprintf */
#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <errno.h>
#include "capi_flash.h"

static const char *version = GIT_VERSION;
static bool quiet = false;
static int verbose = 0;

#define dprintf(fmt, ...) do { \
	if (!quiet) \
		printf( fmt, ## __VA_ARGS__); \
	} while (0)

#define eprintf(fmt, ...) do { \
		fprintf(stderr, "Error: "fmt, ## __VA_ARGS__); \
	} while (0)

#define vprintf(fmt, ...) do { \
	if (verbose > 0) \
		printf( fmt, ## __VA_ARGS__); \
	} while (0)

#define vprintf1(fmt, ...) do { \
	if (verbose > 1) \
		printf( fmt, ## __VA_ARGS__); \
	} while (0)

static int read_config_word(int cfg, int offset, int *retVal)
{
	lseek(cfg, offset, SEEK_SET);
	int ret = read(cfg, retVal, 4);
	if (4 == ret)
		return 0;
	eprintf("read_config_word: 0x%x\n", offset);
	return FLASH_ERR_CFG_READ;   /* Error */
}

static int write_config_word(int cfg, int offset, int data)
{
	int wdata = data;

	lseek(cfg, offset, SEEK_SET);
	int ret = write(cfg, &wdata, 4);
	if (4 == ret)
		return 0;
	eprintf("write_config_word: 0x%x to Adddress: 0x%x\n", data, offset);
	return FLASH_ERR_CFG_WRITE;   /* Error */
}

static int flash_reset(int cfg, int cntl_reg)
{
	int rc;
	rc = write_config_word(cfg, cntl_reg, 0);
	return rc;
}

static int flash_wait_op(int cfg, int cntl_reg, int mask, int wait_cond,
	unsigned timeout)
{
	int rc = 0;
	int config_word = 0x0;
	time_t st, lt, ct;

	st = time(NULL);
	lt = st;

	while (1) {
		rc = read_config_word(cfg, cntl_reg, &config_word);
		if (0 != rc)
			return rc;
		if (FLASH_CHECK_BIT(config_word, mask, wait_cond))
			break;
		ct = time(NULL);
		if ((ct - lt) > 5) {
			printf(".");
			lt = ct;
		}
		if ((ct - st) > timeout) {
			eprintf ("\nFlash not ready after %d min (mask: 0x%x cond: 0x%x)\n",
					timeout/60, mask, wait_cond);
			return FLASH_READY_TIMEOUT;
		}
	}
	return 0;
}

static int flash_reset_wait(int cfg, int cntl_reg)
{
	int rc;
	// -------------------------------------------------------------------------------
	// Reset Any Previously Aborted Sequences
	// -------------------------------------------------------------------------------
	rc = flash_reset(cfg, cntl_reg);
	if (0 != rc) {
		eprintf("Flash Reset\n");
		return rc;
	}
	// -------------------------------------------------------------------------------
	// Wait for Flash to be Ready
	// -------------------------------------------------------------------------------
	rc = flash_wait_op(cfg, cntl_reg, FLASH_READY, FLASH_READY, 120);
	return rc;
}

static int flash_write(int cfg, int cntl_reg, int data_reg, int data)
{
	int rc;
	// ccw added
	// -------------------------------------------------------------------------------
	// Poll for flash port to be ready - offset 0x58 bit 12(LE) = 1 means busy
	// -------------------------------------------------------------------------------
	rc = flash_wait_op(cfg, cntl_reg, FLASH_PORT_READY, 0x0, 30);
	if (0 != rc)
		return rc;
	rc = write_config_word(cfg, data_reg, data);
	return rc;
}

//# -------------------------------------------------------------------------------
//# Setup for Program From Flash
//# -------------------------------------------------------------------------------
static int flash_erase(int cfg, int addr_reg, int size_reg, int cntl_reg,
			int address, int num_blocks)
{
	int rc;

	/* Set Address Reg */
	rc = write_config_word(cfg, addr_reg, address);
	if (0 != rc)
		return rc;
	/* Set size of transfer to flash in blocks */
	rc = write_config_word(cfg, size_reg, num_blocks);
	if (0 != rc)
		return rc;
	/* Send program request to flash */
	rc = write_config_word(cfg, cntl_reg, FLASH_PROG_REQ);
	if (0 != rc)
		return rc;
	//# -------------------------------------------------------------------------------
	//# Wait for Flash Erase to complete.
	//# -------------------------------------------------------------------------------
	rc = flash_wait_op(cfg, cntl_reg, FLASH_ERASE_STATUS | FLASH_PROG_STATUS,
		FLASH_PROG_STATUS, 240);
	return rc;
}

/* Search vsec_offset and config_word in PCI Extended Capabilities */
static int search_capi_vsec(int cfg, int *rc_vsec_offset, int *rc_config_word)
{
	int rc = 0;
	// Search PCI Extended Capabilities list for CAPI VSEC offset
	int next_ecap = PCI_ECAP, ecap_offset = PCI_ECAP;
	int config_word = 0x0;
	*rc_vsec_offset = 0;   /* Set to some invalid address */
	*rc_config_word = 0;

	while (next_ecap != 0x0) {
		rc = read_config_word(cfg, ecap_offset, &config_word);
		if (0 != rc) {
			eprintf("Can not Read ecap_offset1 @0x%x\n", ecap_offset);
			return rc;
		}
		next_ecap = ECAP_NEXT(config_word);
		if ( ECAP_ID(config_word) == ECAP_VSEC ) {
				// Read vsec length/revision/ID
				rc = read_config_word(cfg, ecap_offset + 0x4, &config_word);
				if (0 != rc) {
					eprintf("Can not Read ecap_offset2 @0x%x\n", ecap_offset+4);
					return rc;
				}
				if (VSEC_ID(config_word) == CAPI_VSECID) {
					*rc_vsec_offset = ecap_offset;
					*rc_config_word = config_word;
					return 0;   /* Found */
				}
		}
		ecap_offset = next_ecap;
	}
	eprintf("Unable to find CAPI VSEC\n");
	return -1;
}

/* Set Read Address */
static int flash_set_read_addr(int cfg, int addr_reg, int size_reg, int cntl_reg,
					int raddress, int r_size)
{
	int rc;

	rc = flash_reset_wait(cfg, cntl_reg);
	if (0 != rc)
		return rc;
	//# -------------------------------------------------------------------------------
	//# Setup for Reading From Flash
	//# -------------------------------------------------------------------------------
	rc = write_config_word(cfg, addr_reg, raddress);
	if (0 != rc)
		return rc;
	// Set read size to 512 words
	rc = write_config_word(cfg, size_reg, r_size -1);
	if (0 != rc)
		return rc;
	rc = write_config_word(cfg, cntl_reg, FLASH_READ_REQ);
	return rc;
}

/*
 * poll control register until data count matches expected value
 * if data register is read too soon, previous data value will be
 * captured and remaining data is then shifted
 */
static int flash_wait_ready(int cfg, int cntl_reg, int cntl_remain)
{
	int rc = 0;
	int data;
	int cntl_retry = 100;

	while (cntl_retry > 0) {
		rc = read_config_word(cfg, cntl_reg, &data);
		if (0 != rc)
			return rc;
		if (cntl_remain == (data & 0x3ff))
			break;
		cntl_retry--;
	}
	if (0 == cntl_retry) {
		eprintf("CNTL Retry timeout after 100 reties (0x%x)\n", data);
		return FLASH_READY_TIMEOUT;
	}
	return 0;
}

static void help(const char *prog)
{
	printf("Usage: %s [options]\n"
		"      -h, --help       This help\n"
		"      -v, --verbose    More Verbose\n"
		"      -V, --version    Print version\n"
		"      -q, --quiet      No messages\n"
		"      -p, --factory    Program Factory (primary) Side (NEW)\n"
		"      -a, --address    Flash Address (default: 0x%8.8x)\n"
		"      -b, --blocksize  Flash Block Size (default: %d KB)\n"
		"      -C, --card       Capi Card number (default: %d)\n"
		"      -f, --file       File to flash\n\n", prog,
		DEFAULT_USER_FLASH_ADDRESS, DEFAULT_BLOCK_SIZE, DEFAULT_CAPI_CARD);
}

int main (int argc, char *argv[])
{
	int dat, dif, edat;
	int CFG = -1;
	int FPGA_BIN = -1;
	time_t eet, set, ept, spt, svt, evt;
	int address, raddress;
	int cntl_remain = 0;
	int rc = -1;
	int flash_words;

	int addr_reg, size_reg, cntl_reg, data_reg;
	cntl_reg = 0;
	char *cfg_file = NULL;
	int  print_cnt = 0;
	set = time(NULL);  /* Start Erase Time */
	eet = ept = spt = svt = evt = set;

	int card_no = DEFAULT_CAPI_CARD;
	int flash_address = DEFAULT_USER_FLASH_ADDRESS;  // 0x02000000;
	int flash_block_size = DEFAULT_BLOCK_SIZE;       // 256 KB;

	bool factory = false;
	const char *fpga_file = NULL;
	int cmd;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "verbose",   no_argument,       NULL, 'v' },
			{ "help",      no_argument,       NULL, 'h' },
			{ "version",   no_argument,       NULL, 'V' },
			{ "quiet",     no_argument,       NULL, 'q' },
			{ "card",      required_argument, NULL, 'C' },
			{ "address",   required_argument, NULL, 'a' },
			{ "blocksize", required_argument, NULL, 'b' },
			{ "file",      required_argument, NULL, 'f' },
			{ "factory",   required_argument, NULL, 'p' },
			{ 0,           no_argument,       NULL, 0   },
		};
		cmd = getopt_long(argc, argv, "vhVqpC:a:b:f:",
				long_options, &option_index);
		if (-1 == cmd) break;   /* all params processed ? */ 
		switch (cmd) {
		case 'v':
			verbose++;
			break;
		case 'V':
			printf("Version : %s\n", version);
			exit(0);
			break;
		case 'h':
			help(argv[0]);
			exit(0);
			break;
		case 'q':
			quiet = true;
			break;
		case 'C':
			card_no = strtol(optarg, (char **)NULL, 0);
			break;
		case 'a':
			flash_address = strtol(optarg, (char **)NULL, 0);
			break;
		case 'b':
			flash_block_size = strtol(optarg, (char **)NULL, 0);
			break;
		case 'f':
			fpga_file = optarg;
			break;
		case 'p':  /* factory */
			factory = true;
			break;
		default:
			eprintf("%s Invalid Option\n", argv[0]);
			help(argv[0]);
			exit(0);
		}
	}

	if (NULL == fpga_file) {
		eprintf("%s Missing Option -f -a -b and -C must be set\n", argv[0]);
		help(argv[0]);
		rc = EINVAL;
		goto __exit0;
	}
	/* Set Address to 0 in case factory flag is set */
	if (factory)
		flash_address = 0;
	if ((FPGA_BIN = open(fpga_file, O_RDONLY)) < 0) {
		perror("Error");
		eprintf("Can not open %s\n", fpga_file);
		rc = ENOENT;
		goto __exit0;
	}

	if (asprintf(&cfg_file, CXL_SYSFS_PATH"%d"CXL_CONFIG, card_no) == -1) {
		perror("Error");
		eprintf("Can not Create: "CXL_SYSFS_PATH);
		rc = ENOMEM;
		goto __exit0;
	}
	if ((CFG = open(cfg_file, O_RDWR)) < 0) {
		perror("Error");
		eprintf("Can not open %s\n", cfg_file);
		rc = EACCES;
		goto __exit0;
	}

	int config_word = 0;
	int sub_dev = 0;
	int vsec_offset = 0;

	vprintf1("CAPI CFG Dir     : %s\n", cfg_file);
	vprintf1("File to Flash    : %s\n", fpga_file);
	vprintf1("Flash Address    : 0x%x\n", flash_address);
	vprintf1("Flash Block Size : %d (*1024 Bytes)\n", flash_block_size);
	vprintf1("Quiet Flag       : %d\n", quiet);
	vprintf1("Verbose Flag     : %d\n", quiet);
	vprintf1("Factory Flag     : %d\n", factory);

	if (read_config_word(CFG, PCI_ID, &config_word))
		goto __exit;
	if (read_config_word(CFG, 0x2C, &sub_dev))
		goto __exit;

	int vendor = PCI_VENDORID(config_word);
	int device = PCI_DEVICEID(config_word);

	vprintf("Vendor ID: %04X\n", vendor);
	vprintf("  Device / Sub Device ID: %04X / %04X\n",
			device, PCI_DEVICEID(sub_dev));
	// Check for known CAPI device
	if ( (vendor != IBM_PCIID) || (( device != CAPI_PCIID) && (device != CAPI_LEGACY0) && (device != CAPI_LEGACY1))) {
		eprintf("Unknown Vendor (0x%x) or Device ID (0x%x)\n", vendor, device);
		goto __exit;
	}

	if (0 != search_capi_vsec(CFG, &vsec_offset, &config_word))
		goto __exit;
	vprintf1("VSEC Offset: 0x%03X\n", vsec_offset);
	// Get VSEC size and revision
	int vsec_rev = VSEC_REV(config_word);
	int vsec_size = VSEC_LENGTH(config_word);

	vprintf1("VSEC Length: 0x%03X\nVSEC ID: 0x%1X\n", vsec_size, vsec_rev);
	// Set address for flash registers
	if (vsec_size == 0x80) {
		vprintf("    Version 0.12\n");
		addr_reg = vsec_offset + FLASH_ADDR_OFFSET;
		size_reg = vsec_offset + FLASH_SIZE_OFFSET;
		cntl_reg = vsec_offset + FLASH_CNTL_OFFSET;
		data_reg = vsec_offset + FLASH_DATA_OFFSET;
	} else {
		// Hard code register values for legacy devices
		vprintf("    Version 0.10\n");
		addr_reg = 0x920;
		size_reg = 0x924;
		cntl_reg = 0x928;
		data_reg = 0x92c;
	}
	vprintf1("Addr reg: 0x%03X\nSize reg: 0x%03X\nCntl reg: 0x%03X\n"
		"Data reg: 0x%03X\n", addr_reg, size_reg, cntl_reg, data_reg); 

	// Set stdout to autoflush
	setvbuf(stdout, NULL, _IONBF, 0);

	off_t fsize;
	struct stat tempstat;
	int num_blocks, flash_block_size_words;
	// Flash word size is 4B words
	address = flash_address >> 2;
	// Find size of FPGA binary
	if (stat(fpga_file, &tempstat) != 0) {
		perror("Error");
		eprintf("Cannot determine size of %s\n", fpga_file);
		goto __exit;
	}
	fsize = tempstat.st_size;

	num_blocks = fsize / (flash_block_size * 1024);
	// Size of flash block in words. Flash word = 4B
	flash_block_size_words = flash_block_size * 1024 / 4;
	dprintf("Programming User Partition (@ 0x%08X) with %ld Bytes from File: %s\n",
			address, fsize, fpga_file);
	dprintf("  Program -> for Size: %d in blocks (%dK Words or %dK Bytes)\n\n",num_blocks,
		flash_block_size/4 , flash_block_size);

	dprintf("Reset Flash\n");
	if (0 != flash_reset_wait(CFG, cntl_reg))
		goto __exit;

	dprintf("Erasing Flash\n");
	rc = flash_erase(CFG, addr_reg, size_reg, cntl_reg,
			address, num_blocks);
	eet = time(NULL);  /* End Erase Time */
	spt = ept = svt = evt = eet;
	if (0 != rc)
		goto __exit;
	/* Number of Write Words (each does have 4 Bytes)  */
	flash_words = flash_block_size_words * (num_blocks + 1);

	//# -------------------------------------------------------------------------------
	//# Program Flash
	//# -------------------------------------------------------------------------------
	dprintf("\n\nProgramming Flash\n");

	int bc = 0;
	int i;
	dprintf("Writing Block:");

	for (i = 0; i < flash_words; i++) {
		dif = read(FPGA_BIN, &dat, 4);
		if (!(dif))
			dat = 0xFFFFFFFF;    /* Provide data for EOF */
		rc = flash_write(CFG, cntl_reg, data_reg, dat);
		ept = time(NULL);
		svt = evt = ept;
		if (0 != rc)
			goto __exit;
		if (((i+1) % (flash_block_size_words)) == 0) {
			dprintf(" %d",bc);
			bc++;
		}
	}
	printf("\n");

	//# -------------------------------------------------------------------------------
	//# Wait for Flash Program to complete.
	//# -------------------------------------------------------------------------------
	rc = flash_wait_op(CFG, cntl_reg, FLASH_OP_DONE, FLASH_OP_DONE, 120);
	ept = time(NULL);
	svt = evt = ept;
	if (0 != rc)
		goto __exit;
	//# -------------------------------------------------------------------------------
	//# Reset and wait
	//# -------------------------------------------------------------------------------
	rc =  flash_reset_wait(CFG, cntl_reg);
	evt =  time(NULL);
	if (0 != rc)
		goto __exit;
	dprintf("\n");

	//# -------------------------------------------------------------------------------
	//# Verify Flash Programmming
	//# -------------------------------------------------------------------------------
	dprintf("Verifying Flash\n");

	lseek(FPGA_BIN, 0, SEEK_SET);   // Reset to beginning of file
	svt = time(NULL);               // Get Start Verify Time
	bc = 0;
	raddress = address;

	dprintf("Reading Block:");
	for( i = 0; i < flash_words; i++) {
		evt = time(NULL);
		dif = read(FPGA_BIN,&edat,4);
		if (!(dif))
			edat = 0xFFFFFFFF;
		// At 512 word read size.
		if ((i % 512) == 0) {
			rc = flash_set_read_addr(CFG, addr_reg, size_reg, cntl_reg,
					raddress, FLASH_READ_SIZE);
			if (0 != rc)
				goto __exit;
			raddress += FLASH_READ_SIZE;
			cntl_remain = FLASH_READ_SIZE -1;
		}

		cntl_remain = (cntl_remain - 1) & 0x3ff;
		rc = flash_wait_ready(CFG, cntl_reg, cntl_remain);
		if (0 != rc)
			goto __exit;
		// Read data from flash
		rc = read_config_word(CFG, data_reg, &dat);
		if (0 != rc)
			goto __exit;

		if ((edat != dat) && (print_cnt < 1024)) {
			int ma = raddress + (i % 512) - 0x200;
			eprintf("Data Miscompare @: %08x --> %08x expected %08x\n",ma, dat, edat);
			print_cnt++;
		}

		if (((i+1) % (flash_block_size_words)) == 0) {
			printf(" %d", bc);
			bc++;
		}
	}
	rc = 0;            /* Good */

__exit:
	dprintf("\n");
	evt = time(NULL);  /* Get End of Verifaction time */
	//# -------------------------------------------------------------------------------
	//# Calculate and Print Elapsed Times
	//# -------------------------------------------------------------------------------
	dprintf("Erase Time:   %d seconds\n", (int)(eet - set));
	dprintf("Program Time: %d seconds\n", (int)(ept - spt));
	dprintf("Verify Time:  %d seconds\n", (int)(evt - svt));
	dprintf("Total Time:   %d seconds\n", (int)(evt - set));
	dprintf("Flash RC: %d\n", rc);

__exit0:
	if (-1 != FPGA_BIN)
		close(FPGA_BIN);
	if (-1 != CFG) {
		if (0 != cntl_reg)
			flash_reset(CFG, cntl_reg);
		close(CFG);
	}
	if (cfg_file)
		free(cfg_file);
	return rc;
}
