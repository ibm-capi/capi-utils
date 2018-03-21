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

#include "capi_flash.h"

static int read_config_word(int cfg, int offset, int *retVal)
{
	lseek(cfg, offset, SEEK_SET);
	int ret = read(cfg, retVal, 4);
	if (4 == ret)
		return 0;
	printf("ERROR read_config_word: 0x%x\n", offset);
	return FLASH_ERR_CFG_READ;   /* Error */
}

static int write_config_word(int cfg, int offset, int data)
{
	int wdata = data;

	lseek(cfg, offset, SEEK_SET);
	int ret = write(cfg, &wdata, 4);
	if (4 == ret)
		return 0;
	printf("ERROR write_config_word: 0x%x to Adddress: 0x%x\n", data, offset);
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
			printf ("\nFAILURE --> Flash not ready after %d min (mask: 0x%x cond: 0x%x)\n",
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
		printf("Flash Reset Error\n");
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
			printf("Can not Read ecap_offset1 @0x%x\n", ecap_offset);
			return rc;
		}
		next_ecap = ECAP_NEXT(config_word);
		if ( ECAP_ID(config_word) == ECAP_VSEC ) {
				// Read vsec length/revision/ID
				rc = read_config_word(cfg, ecap_offset + 0x4, &config_word);
				if (0 != rc) {
					printf("Can not Read ecap_offset2 @0x%x\n", ecap_offset+4);
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
	printf("Unable to find CAPI VSEC\n");
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
		printf("CNTL Retry timeout after 100 CNTL Reg was 0x%x\n", data);
		return FLASH_READY_TIMEOUT;
	}
	return 0;
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

	char fpga_file[MAX_STRING_SIZE];
	char cfg_file[MAX_STRING_SIZE];

	int  print_cnt = 0;
	set = time(NULL);  /* Start Erase Time */
	eet = ept = spt = svt = evt = set;

	if (argc != 5) {
		printf("Usage: capi_flash <fpga_binary_file> <card#> <flash_address> <flash_block_size>\n\n");
		goto __exit;
	}
	strncpy (fpga_file, argv[1], MAX_STRING_SIZE);

	if ((FPGA_BIN = open(fpga_file, O_RDONLY)) < 0) {
		printf("Can not open %s\n",fpga_file);
		goto __exit;
	}

	strcpy(cfg_file, CXL_SYSFS_PATH);
	// CXL card # should not be larger then 2 characters (ie < 100)
	strncat(cfg_file, argv[2], 2);
	strcat(cfg_file, CXL_CONFIG);

	if ((CFG = open(cfg_file, O_RDWR)) < 0) {
		printf("Can not open %s\n",cfg_file);
		goto __exit;
	}

	int flash_address = strtol(argv[3], NULL, 16);
	int flash_block_size = atoi(argv[4]);
	int config_word = 0;
	int vsec_offset = 0;

	printf("File:    %s\n", fpga_file);
	printf("Card:    %s\n", cfg_file);
	printf("Address: %x\n", flash_address);
	printf("Block:   %d\n", flash_block_size);

	if (read_config_word(CFG, PCI_ID, &config_word))
		goto __exit;

	int vendor = PCI_VENDORID(config_word);
	int device = PCI_DEVICEID(config_word);
	printf("Device ID: %04X\n", device);
	printf("Vendor ID: %04X\n", vendor);
	// Check for known CAPI device
	if ( (vendor != IBM_PCIID) || (( device != CAPI_PCIID) && (device != CAPI_LEGACY0) && (device != CAPI_LEGACY1))) {
		printf("Unknown Vendor (0x%x) or Device ID (0x%x)\n", vendor, device);
		goto __exit;
	}

	if (0 != search_capi_vsec(CFG, &vsec_offset, &config_word))
		goto __exit;
	printf("VSEC Offset: 0x%03X\n", vsec_offset);
	// Get VSEC size and revision
	int vsec_rev = VSEC_REV(config_word);
	int vsec_size = VSEC_LENGTH(config_word);
	printf("VSEC Length: 0x%03X\nVSEC ID: 0x%1X\n", vsec_size, vsec_rev);
	// Set address for flash registers
	if (vsec_size == 0x80) {
		printf("    Version 0.12\n\n");
		addr_reg = vsec_offset + FLASH_ADDR_OFFSET;
		size_reg = vsec_offset + FLASH_SIZE_OFFSET;
		cntl_reg = vsec_offset + FLASH_CNTL_OFFSET;
		data_reg = vsec_offset + FLASH_DATA_OFFSET;
	} else {
		// Hard code register values for legacy devices
		printf("    Version 0.10\n\n");
		addr_reg = 0x920;
		size_reg = 0x924;
		cntl_reg = 0x928;
		data_reg = 0x92c;
	}
	printf("Addr reg: 0x%03X\nSize reg: 0x%03X\nCntl reg: 0x%03X\n"
		"Data reg: 0x%03X\n", addr_reg, size_reg, cntl_reg, data_reg); 
	// Set stdout to autoflush
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("\n");

	off_t fsize;
	struct stat tempstat;
	int num_blocks, flash_block_size_words;
	// Flash word size is 4B words
	address = flash_address >> 2;
	// Find size of FPGA binary
	if (stat(fpga_file, &tempstat) != 0) {
		fprintf(stderr, "Cannot determine size of %s: %s\n", fpga_file, strerror(errno));
		goto __exit;
	}
	fsize = tempstat.st_size;

	num_blocks = fsize / flash_block_size;
	// Size of flash block in words. Flash word = 4B
	flash_block_size_words = flash_block_size / 4;
	printf("Programming User Partition (@ 0x%08X) with %ld Bytes from File: %s\n",
			address, fsize, fpga_file);
	printf("  Program -> for Size: %d in blocks (%dK Words or %dK Bytes)\n\n",num_blocks,
		(flash_block_size / (4 * 1024)), flash_block_size / 1024);

	printf("Reset Flash\n");
	if (0 != flash_reset_wait(CFG, cntl_reg))
		goto __exit;

	printf("Erasing Flash\n");
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
	printf("\n\nProgramming Flash\n");

	int bc = 0;
	int i;
	printf("Writing Block:");

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
			printf(" %d",bc);
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
	printf("\n");

	//# -------------------------------------------------------------------------------
	//# Verify Flash Programmming
	//# -------------------------------------------------------------------------------
	printf("Verifying Flash\n");

	lseek(FPGA_BIN, 0, SEEK_SET);   // Reset to beginning of file
	svt = time(NULL);               // Get Start Verify Time
	bc = 0;
	raddress = address;

	printf("Reading Block:");
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
			printf("Data Miscompare @: %08x --> %08x expected %08x\n",ma, dat, edat);
			print_cnt++;
		}

		if (((i+1) % (flash_block_size_words)) == 0) {
			printf(" %d", bc);
			bc++;
		}
	}
	rc = 0;            /* Good */

__exit:
	printf("\n");
	evt = time(NULL);  /* Get End of Verifaction time */
	//# -------------------------------------------------------------------------------
	//# Calculate and Print Elapsed Times
	//# -------------------------------------------------------------------------------
	printf("Erase Time:   %d seconds\n", (int)(eet - set));
	printf("Program Time: %d seconds\n", (int)(ept - spt));
	printf("Verify Time:  %d seconds\n", (int)(evt - svt));
	printf("Total Time:   %d seconds\n", (int)(evt - set));
	printf("Flash RC: %d\n", rc);

	if (-1 != FPGA_BIN)
		close(FPGA_BIN);
	if (-1 != CFG) {
		if (0 != cntl_reg)
			flash_reset(CFG, cntl_reg);
		close(CFG);
	}
	return rc;
}
