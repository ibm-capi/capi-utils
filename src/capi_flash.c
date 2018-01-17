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

int flash_reset(int cfg, int cntl_reg)
{
  int config_word = 0x0;
  int retVal = write_config_word(cfg, cntl_reg, &config_word);
  return (retVal>0)?0:retVal;
}

int flash_wait_ready(int cfg, int cntl_reg)
{
  int config_word = 0x0; 

 time_t st, lt, ct;

  st = time(NULL);
  lt = st;
  int error_code = 0;

  while (!FLASH_CHECK_BIT(config_word, FLASH_READY)) 
  {
    read_config_word(cfg, cntl_reg, &config_word);
    ct = time(NULL);
    if ((ct - lt) > 5) {
      printf(".");
      lt = ct;
    }
    if ((ct - st) > 120) {
      printf ("\nFAILURE --> Flash not ready after 2 min\n");
      error_code = 1;
      break;
    }
  }
  return error_code;
}

int flash_erase_wait(int cfg, int cntl_reg)
{
  time_t st, lt, ct;
  st = lt = time(NULL);

  int config_word = 0;
  int error_code = 0;

  while (!(((config_word & FLASH_ERASE_STATUS) == 0x0) 
      && ((config_word & FLASH_PROG_STATUS) == FLASH_PROG_STATUS))) 
  {
    read_config_word(cfg, cntl_reg, &config_word);
    ct = time(NULL);
    if ((ct - lt) > 1) {
      printf(".");
      lt = ct;
    }
    if ((ct - st) > 240) {
      printf ("\nFAILURE --> Erase did not complete after 4 min\n");
      error_code = 2;
      break;
    }
  }
  return error_code;
}

int flash_port_ready(int cfg, int cntl_reg)
{
  time_t st, lt, ct;
  st = time(NULL);
  lt = st;
  // Initialize to "busy"
  int config_word = FLASH_PORT_READY;
  int error_code = 0;

  while (FLASH_CHECK_BIT(config_word, FLASH_PORT_READY)) {
    read_config_word(cfg, cntl_reg, &config_word);
	  ct = time(NULL);
	  if ((ct - lt) > 5) {
	    printf(".");
	    lt = ct;
	  }
	  if ((ct - st) > 30) {
	    printf ("\nFAILURE --> Flash Port not ready after 30 seconds\n");
	    error_code = 99;
      break;
	  }
  }
  return error_code;
}

int flash_wait_program(int cfg, int cntl_reg)
{
  int config_word = 0x0;
  int error_code = 0;

  time_t st, lt, ct;

  st = lt = time(NULL);

  while (!FLASH_CHECK_BIT(config_word, FLASH_OP_DONE)) {
    read_config_word(cfg, cntl_reg, &config_word);
    ct = time(NULL);
    if ((ct - lt) > 5) {
	    printf(".");
	    lt = ct;
    } 
    if ((ct - st) > 120)
    { 
      printf ("\nFAILURE --> Programming did not complete after 2 min\n");
	    error_code = 4;
      break;
    }
  }
  return error_code;
}

int main (int argc, char *argv[])
{
  int dat, dif, edat;
  int CFG;
  int FPGA_BIN;
  time_t et, eet, set, ept, spt, svt, evt;
  int address, raddress;

  char fpga_file[MAX_STRING_SIZE];
  char cfg_file[MAX_STRING_SIZE];

  int  print_cnt = 0;

  if (argc < 3) {
    printf("Usage: capi_flash <fpga_binary_file> <card#>\n\n");
    exit(-1);
  }
  strncpy (fpga_file, argv[1], MAX_STRING_SIZE);

  if ((FPGA_BIN = open(fpga_file, O_RDONLY)) < 0) {
    printf("Can not open %s\n",fpga_file);
    exit(-1);
  }

  strcpy(cfg_file, CXL_SYSFS_PATH);
  // CXL card # should not be larger then 2 characters (ie < 100)
  strncat(cfg_file, argv[2], 2);
  strcat(cfg_file, CXL_CONFIG);

  if ((CFG = open(cfg_file, O_RDWR)) < 0) {
    printf("Can not open %s\n",cfg_file);
    exit(-1);
  }

  int config_word;
  
  CHECKIO( read_config_word(CFG, PCI_ID, &config_word) ); 
  int vendor = PCI_VENDORID(config_word);  
  int device = PCI_DEVICEID(config_word);  
  printf("Device ID: %04X\n", device);
  printf("Vendor ID: %04X\n", vendor);
  // Check for known CAPI device
  if ( (vendor != IBM_PCIID) || (( device != CAPI_PCIID) && (device != CAPI_LEGACY0) && (device != CAPI_LEGACY1))) {
	  printf("Unknown Vendor or Device ID\n");
	  exit(-1);
  }
  // Search PCI Extended Capabilities list for CAPI VSEC offset
  int vsec_offset = 0x0, next_ecap = PCI_ECAP, ecap_offset = PCI_ECAP;
  config_word = 0x0;
  while (next_ecap != 0x0)
  {
    CHECKIO( read_config_word(CFG, ecap_offset, &config_word) );
    next_ecap = ECAP_NEXT(config_word);
    if ( ECAP_ID(config_word) == ECAP_VSEC )
    {
      // Read vsec length/revision/ID
      CHECKIO( read_config_word(CFG, ecap_offset + 0x4, &config_word) );
      if (VSEC_ID(config_word) == CAPI_VSECID) 
      { 
        vsec_offset = ecap_offset;
        break;
      }
    }
    ecap_offset = next_ecap;
  }
  if (vsec_offset == 0x0)
  {
    printf("Unable to find CAPI VSEC\n");
    exit(-1);
  }
  printf("VSEC Offset: 0x%03X\n", vsec_offset);
  // Get VSEC size and revision
  int vsec_rev = VSEC_REV(config_word);
  int vsec_size = VSEC_LENGTH(config_word);
  printf("VSEC Length: 0x%03X\nVSEC ID: 0x%1X\n", vsec_size, vsec_rev);
  // Set address for flash registers
  int addr_reg, size_reg, cntl_reg, data_reg;
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

// -------------------------------------------------------------------------------
// Reset Any Previously Aborted Sequences
// -------------------------------------------------------------------------------
  CHECK( flash_reset(CFG, cntl_reg) );

// -------------------------------------------------------------------------------
// Wait for Flash to be Ready
// -------------------------------------------------------------------------------

  CHECK( flash_wait_ready(CFG, cntl_reg) );

  printf("\n");


  // TODO remove
  int ku3_address   = 0x2000000;
  // KU3 blocksize: 256KB
  int ku3_blocksize = 4 * 64 * 1024; 
  //

  // Setup flash partition to write to
  off_t fsize;
  struct stat tempstat;
  int num_blocks;
  // Flash word size is 4B words
  address = ku3_address >> 2;  //user partion.
  // Find size of FPGA binary
  if (stat(fpga_file, &tempstat) != 0) {
    fprintf(stderr, "Cannot determine size of %s: %s\n", fpga_file, strerror(errno));
    exit(-1);
  } else {
    fsize = tempstat.st_size;
  }

  num_blocks = fsize / ku3_blocksize;
  printf("Programming User Partition with %s\n", fpga_file);
  printf("  Program ->  for Size: %d in blocks (%dK Words or %dK Bytes)\n\n",num_blocks, 
      (ku3_blocksize / (4 * 1024)), ku3_blocksize / 1024);
  set = time(NULL);  
  //# -------------------------------------------------------------------------------
  //# Setup for Program From Flash
  //# -------------------------------------------------------------------------------

  // Set flash address to write to
  CHECKIO( write_config_word(CFG, addr_reg, &address) );
  // Set size of transfer to flash in blocks
  CHECKIO( write_config_word(CFG, size_reg, &num_blocks) );
  // Send program request to flash
  config_word = FLASH_PROG_REQ;
  CHECKIO( write_config_word(CFG, cntl_reg, &config_word) );

  printf("Erasing Flash\n");

  //# -------------------------------------------------------------------------------
  //# Wait for Flash Erase to complete.
  //# -------------------------------------------------------------------------------
  CHECK( flash_erase_wait(CFG, cntl_reg) );  

  printf("\n");

  eet = spt = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Program Flash                        
  //# -------------------------------------------------------------------------------
  printf("\n\nProgramming Flash\n");
  
  int bc = 0;
  int i;
  printf("Writing Block: %d        \r", bc);
  for(i=0; i<(64*1024*(num_blocks+1)); i++) {
    dif = read(FPGA_BIN,&dat,4);
    if (!(dif)) {
      dat = 0xFFFFFFFF;
    }

    // ccw added
    // -------------------------------------------------------------------------------
    // Poll for flash port to be ready - offset 0x58 bit 12(LE) = 1 means busy
    // -------------------------------------------------------------------------------
    CHECK( flash_port_ready(CFG, cntl_reg) );

    CHECKIO( write_config_word(CFG, data_reg, &dat) );
    
    if (((i+1) % (256)) == 0) {
      printf("Writing Buffer: %d        \r",bc);
      bc++;
    }
  }

  printf("\n\n");

  //# -------------------------------------------------------------------------------
  //# Wait for Flash Program to complete.
  //# -------------------------------------------------------------------------------
  CHECK( flash_wait_program(CFG, cntl_reg) );  
  printf("\n");

  ept = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Reset Program Sequence
  //# -------------------------------------------------------------------------------
  CHECK( flash_reset(CFG, cntl_reg) );

  //# -------------------------------------------------------------------------------
  //# Wait for Flash to be ready
  //# -------------------------------------------------------------------------------
  CHECK( flash_wait_ready(CFG, cntl_reg) );
  printf("\n");

  svt = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Verify Flash Programmming
  //# -------------------------------------------------------------------------------

  printf("Verifying Flash\n");

  lseek(FPGA_BIN, 0, SEEK_SET);   // Reset to beginning of file

  bc = 0;
  raddress = address;
  for(i=0; i<(64*1024*(num_blocks+1)); i++) {

    dif = read(FPGA_BIN,&edat,4);
    if (!(dif)) {
      edat = 0xFFFFFFFF;
    }

    if ((i % 512) == 0) {
      CHECK( flash_reset(CFG, cntl_reg) );

      //# -------------------------------------------------------------------------------
      //# Wait for Flash to be ready
      //# -------------------------------------------------------------------------------
      CHECK( flash_wait_ready(CFG, cntl_reg) );

      //# -------------------------------------------------------------------------------
      //# Setup for Reading From Flash
      //# -------------------------------------------------------------------------------
      CHECKIO( write_config_word(CFG, addr_reg, &raddress) );
      raddress += 0x200;
      
      dat = 0x1FF;
      CHECKIO( write_config_word(CFG, size_reg, &dat) );
      
      dat = 0x08000000;
      CHECKIO( write_config_word(CFG, cntl_reg, &dat) );
    }
    // Read data from flash
    CHECKIO( read_config_word(CFG, data_reg, &dat) );
  
    if ((edat != dat) && (print_cnt < 1024)) {
      int ma = raddress + (i % 512) - 0x200;
      printf("Data Miscompare @: %08x --> %08x expected %08x\n",ma, dat, edat);
      print_cnt++;
    }
  
    if (((i+1) % (64*1024)) == 0) {
      printf("Reading Block: %d        \r", bc);
      bc++;
    }
  }
  evt = time(NULL);
  printf("\n\n");


  //# -------------------------------------------------------------------------------
  //# Calculate and Print Elapsed Times
  //# -------------------------------------------------------------------------------
  et = evt - set;
  eet = eet - set;
  ept = ept - spt;
  evt = evt - svt;
  
  printf("Erase Time:   %d seconds\n", (int)eet);
  printf("Program Time: %d seconds\n", (int)ept);
  printf("Verify Time:  %d seconds\n", (int)evt);
  printf("Total Time:   %d seconds\n\n", (int)et);

  //# -------------------------------------------------------------------------------
  //# Reset Read Sequence
  //# -------------------------------------------------------------------------------
  CHECK( flash_reset(CFG, cntl_reg) );

  close(FPGA_BIN);
  close(CFG);

  return 0;

}

