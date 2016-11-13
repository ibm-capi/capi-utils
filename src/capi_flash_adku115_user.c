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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <endian.h>
#include <time.h>
#include <assert.h>

#define CHECK(X) (assert((X) >= 0))

int main (int argc, char *argv[])
{
  // int priv1,priv2;
  int dat, dif, edat;
  int CFG;
  int RBF;
  time_t ct, lt, st, et, eet, set, ept, spt, svt, evt;
  int address, raddress;

  char rbf_file[1024];
  char cfg_file[1024];

  int  print_cnt = 0;

  if (argc < 3) {
    printf("Usage: capi_flash <rbf_file> <card#>\n\n");
    exit(-1);
  }
  strcpy (rbf_file, argv[1]);

  if ((RBF = open(rbf_file, O_RDONLY)) < 0) {
    printf("Can not open %s\n",rbf_file);
    exit(-1);
  }

  strcpy(cfg_file, "/sys/class/cxl/card");
  strcat(cfg_file, argv[2]);
  strcat(cfg_file, "/device/config");

  if ((CFG = open(cfg_file, O_RDWR)) < 0) {
    printf("Can not open %s\n",cfg_file);
    exit(-1);
  }

  int temp,vendor,device, subsys;
  lseek(CFG, 0, SEEK_SET);
  CHECK( read(CFG, &temp, 4) );
  vendor = temp & 0xFFFF;
  device = (temp >> 16) & 0xFFFF;  
  //printf("Device ID: %04X\n", device);
  //printf("Vendor ID: %04X\n", vendor);

  lseek(CFG, 44, SEEK_SET);
  CHECK( read(CFG, &temp, 4) );
  subsys = (temp >> 16) & 0xFFFF;

  if ( (vendor != 0x1014) || ( device != 0x0477) || (subsys != 0x0608)) {
	  printf("Not the correct flsahing binary for card\n");
	  exit(-1);
  }
  
  int addr_reg, size_reg, cntl_reg, data_reg;
  lseek(CFG, 0x404, SEEK_SET);
  CHECK( read(CFG, &temp,4) );
  printf("  VSEC Length/VSEC Rev/VSEC ID: 0x%08X\n", temp);
  if ( (temp & 0x08000000) == 0x08000000 ) {
    printf("    Version 0.12\n\n");
    addr_reg = 0x450;
    size_reg = 0x454;
    cntl_reg = 0x458;
    data_reg = 0x45c;
  } else {
    printf("    Version 0.10\n\n");
    addr_reg = 0x920;
    size_reg = 0x924;
    cntl_reg = 0x928;
    data_reg = 0x92c;
  }
 
  // Set stdout to autoflush
  setvbuf(stdout, NULL, _IONBF, 0);

// -------------------------------------------------------------------------------
// Reset Any Previously Aborted Sequences
// -------------------------------------------------------------------------------
  temp = 0;
  lseek(CFG, cntl_reg, SEEK_SET);
  CHECK( write(CFG,&temp,4) );
 

// -------------------------------------------------------------------------------
// Wait for Flash to be Ready
// -------------------------------------------------------------------------------
  lseek(CFG, cntl_reg, SEEK_SET);
  CHECK( read(CFG,&temp,4) );

  st = time(NULL);
  lt = st;
  int ec = 0;
  int cp = 1;

  while (cp == 1) {
    lseek(CFG, cntl_reg, SEEK_SET);
    CHECK( read(CFG,&temp,4) );
    if ( (temp & 0x80000000) == 0x80000000 ) {
      cp = 0;
    }
    ct = time(NULL);
    if ((ct - lt) > 5) {
      printf(".");
      lt = ct;
    }
    if ((ct - st) > 120) {
      printf ("\nFAILURE --> Flash not ready after 2 min\n");
      cp = 0;
      ec = 1;
    }
  }
  printf("\n");

  off_t fsize;
  struct stat tempstat;
  int num_blocks;
  address = 0x0000000;  //user partion.
  if (stat(rbf_file, &tempstat) != 0) {
    fprintf(stderr, "Cannot determine size of %s: %s\n", rbf_file, strerror(errno));
    exit(-1);
  } else {
    fsize = tempstat.st_size;
  }
  num_blocks = fsize / (64 * 1024 *4);
  printf("Programming User Partition with %s\n", rbf_file);
  printf("  Program ->  for Size: %d in blocks (32K Words or 128K Bytes)\n\n",num_blocks);

  set = time(NULL);  
  //# -------------------------------------------------------------------------------
  //# Setup for Program From Flash
  //# -------------------------------------------------------------------------------
  lseek(CFG, addr_reg, SEEK_SET);
  CHECK( write(CFG,&address,4) );
  
  lseek(CFG, size_reg, SEEK_SET);
  CHECK( write(CFG,&num_blocks,4) );

  temp = 0x04000000;
  lseek(CFG, cntl_reg, SEEK_SET);
  CHECK( write(CFG,&temp,4) );

  printf("Erasing Flash\n");

  //# -------------------------------------------------------------------------------
  //# Wait for Flash Erase to complete.
  //# -------------------------------------------------------------------------------
  st = lt = time(NULL);
  cp = 1;

  while (cp == 1) {
    lseek(CFG, cntl_reg, SEEK_SET);
    CHECK( read(CFG,&temp,4) );
    if ( ( (temp & 0x00008000) == 0x00000000 ) &&
         ( (temp & 0x00004000) == 0x00004000 ) ){
      cp = 0;
    }
    ct = time(NULL);
    if ((ct - lt) > 5) {
      printf(".");
      lt = ct;
    }
    if ((ct - st) > 240) {
      printf ("\nFAILURE --> Erase did not complete after 4 min\n");
      cp = 0;
      ec = 2;
    }
  }
  printf("\n");

  eet = spt = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Program Flash                        
  //# -------------------------------------------------------------------------------
  int prtnr = 0;
  if (ec == 0) {
    printf("\n\nProgramming Flash\n");
  
    int bc = 0;
    int i;
    printf("Writing Block: %d        \r", bc);
    for(i=0; i<(64*1024*(num_blocks+1)); i++) {
      dif = read(RBF,&dat,4);
      if (!(dif)) {
        dat = 0xFFFFFFFF;
      }


      // ccw added
      // -------------------------------------------------------------------------------
      // Poll for flash port to be ready - offset 0x58 bit 12(LE) = 1 means busy
      // -------------------------------------------------------------------------------

      st = time(NULL);
      lt = st;
      cp = 1;

      while (cp == 1) {
	      lseek(CFG, cntl_reg, SEEK_SET);
	      CHECK( read(CFG,&temp,4) );
	      if ( (temp & 0x00001000) == 0x00000000 ) {
	        cp = 0;
	      } else {
	        ++prtnr;
	      }

	      ct = time(NULL);
	      if ((ct - lt) > 5) {
	        printf("...");
	        lt = ct;
	      }
	      if ((ct - st) > 30) {
	        printf ("\nFAILURE --> Flash Port not ready after 30 seconds\n");
	        cp = 0;
	        ec = 99;
	        i = (64*1024*(num_blocks+1));
	      }
      }
      // ccw end add

      lseek(CFG, data_reg, SEEK_SET);
      CHECK( write(CFG,&dat,4) );
    
      if (((i+1) % (256)) == 0) {
        printf("Writing Buffer: %d        \r",bc);
        bc++;
      }
    }
  }

  printf("\n\n");
  printf("Port not ready %d times\n", prtnr);

  //# -------------------------------------------------------------------------------
  //# Wait for Flash Program to complete.
  //# -------------------------------------------------------------------------------
  if (ec == 0) {
    st = lt = time(NULL);
    cp = 1;

    while (cp == 1) {
      lseek(CFG, cntl_reg, SEEK_SET);
      CHECK( read(CFG,&temp,4) );
      if ( (temp & 0x40000000) == 0x40000000 ) { 
	      cp = 0;
      }
      ct = time(NULL);
      if ((ct - lt) > 5) {
	      printf(".");
	      lt = ct;
      }
      if ((ct - st) > 120) {
	      printf ("\nFAILURE --> Programming did not complete after 2 min\n");
	      cp = 0;
	      ec = 4;
      }
    }
  }
  printf("\n");

  ept = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Reset Program Sequence
  //# -------------------------------------------------------------------------------
  dat=0;
  lseek(CFG,cntl_reg,SEEK_SET);
  CHECK( write(CFG,&dat,4) );

  //# -------------------------------------------------------------------------------
  //# Wait for Flash to be ready
  //# -------------------------------------------------------------------------------
  st = lt = time(NULL);
  cp = 1;

  if (ec ==0) {
    while (cp == 1) {
      lseek(CFG, cntl_reg, SEEK_SET);
       CHECK( read(CFG,&temp,4) );
      if ( (temp & 0x80000000) == 0x80000000 ) { 
	      cp = 0;
      }
      ct = time(NULL);
      if ((ct - lt) > 5) {
	      printf(".");
	      lt = ct;
      }
      if ((ct - st) > 120) {
	      printf ("\nFAILURE --> Flash not ready after 2 min\n");
	      cp = 0;
	      ec = 6;
      }
    }
  }
  printf("\n");

  svt = time(NULL);

  //# -------------------------------------------------------------------------------
  //# Verify Flash Programmming
  //# -------------------------------------------------------------------------------

  if (ec == 0) {
    printf("Verifying Flash\n");

    lseek(RBF, 0, SEEK_SET);   // Reset to beginning of file

    int i, bc = 0;
    raddress = address;
    //printf("Reading Block: %d        \r",bc);
    for(i=0; i<(64*1024*(num_blocks+1)); i++) {

      dif = read(RBF,&edat,4);
      if (!(dif)) {
        edat = 0xFFFFFFFF;
      }

      if ((i % 512) == 0) {
        dat = 0;
        lseek(CFG, cntl_reg, SEEK_SET);
        CHECK( write(CFG,&dat,4) );

        //# -------------------------------------------------------------------------------
        //# Wait for Flash to be ready
        //# -------------------------------------------------------------------------------
        st = lt = time(NULL);
        cp = 1;
      
        while (cp == 1) {
          lseek(CFG, cntl_reg, SEEK_SET);
          CHECK( read(CFG,&temp,4) );
          if ( (temp & 0x80000000) == 0x80000000 ) { 
            cp = 0;
          }
          ct = time(NULL);
          if ((ct - lt) > 5) {
            printf(".");
            lt = ct;
          }
          if ((ct - st) > 120) {
            printf ("\nFAILURE --> Flash not ready after 2 min\n");
            cp = 0;
            ec = 16;
          }
        }

        //# -------------------------------------------------------------------------------
        //# Setup for Reading From Flash
        //# -------------------------------------------------------------------------------
        lseek(CFG, addr_reg, SEEK_SET);
        CHECK( write(CFG,&raddress,4) );
        raddress += 0x200;
      
        dat = 0x1FF;
        lseek(CFG, size_reg, SEEK_SET);
        CHECK( write(CFG,&dat,4) );
      
        dat = 0x08000000;
        lseek(CFG, cntl_reg, SEEK_SET);
        CHECK( write(CFG,&dat,4) );
      }

      lseek(CFG, data_reg, SEEK_SET);
      CHECK( read(CFG,&dat,4) );
  
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
  }
  evt = time(NULL);
  printf("\n\n");

  if (ec != 0) {
     printf("\nErrors Occured : Error Code => %d\n", ec);
  }

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
  dat = 0;
  lseek(CFG, cntl_reg, SEEK_SET);
  CHECK( write(CFG,&dat,4) );


  close(RBF);
  close(CFG);

  return 0;

}

