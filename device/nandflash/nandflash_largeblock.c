/* Large-block NAND flash emulation
	Copyright 2008, 2009	Haxx Enterprises
	
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
//#include <sys/mman.h>
#include "portable/mman.h"
#include "skyeye_nandflash.h"
#include "nandflash_largeblock.h"

void nandflash_lb_reset(struct nandflash_device *dev);
static void nandflash_lb_doerase(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	u32 len, base, i;
	if (nf->WP!=NF_HIGH) return;

	len = dev->erasesize;
	base = (nf->pagenum & (~0x3f)) * (nf->pagefmtsize);
	for (i=0;i<len;i++)
		nf->addrspace[base+i]=0xFF;
}

static void nandflash_lb_dodatawrite(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	if(nf->WP!=NF_HIGH) return;

	if (nf->pageoffset>dev->pagedumpsize-1)
		NANDFLASH_DBG("nandflash write cycle out bound\n");
	nf->writebuffer[nf->pageoffset]=nf->IOPIN;
	nf->pageoffset++;		
}

static void nandflash_lb_finishwrite(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	u32 i,base;
	if(nf->WP!=NF_HIGH) return;

	base = nf->pagenum * nf->pagefmtsize;
	for (i=0;i<dev->pagedumpsize;i++)
		nf->addrspace[base+i] &= nf->writebuffer[i];
}

static void nandflash_lb_doread(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	//if (!nf->pageoffset) printf("NAND Flash: read page %x offset %x\n", nf->pagenum, nf->pageoffset);
	if ((nf->pagenum * 0x840 + nf->pageoffset)<dev->devicesize)
	{
		nf->IOPIN=*(nf->addrspace+nf->pagenum * nf->pagefmtsize + nf->pageoffset);
		nf->pageoffset++;
		if (nf->pageoffset==0x840) {
			nf->pagenum++;
			nf->pageoffset = 0;
		}

/*		if((nf->address%dev->pagedumpsize==0)&&(nf->cmd==NAND_CMD_READOOB))
		{
			nf->address+=dev->pagesize;
		} */
		//NANDFLASH_DBG("%s:mach:%x,data:%x\n", __FUNCTION__, nf->address,nf->IOPIN);
	}
	else
	{
		NANDFLASH_DBG("nandflash read out of bound!\n");
	}
}

static void nandflash_lb_doreadid(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	NANDFLASH_DBG("nandflash_lb_doreadid\n");
	
	switch(nf->cmdstatus){
	case NF_readID_1st:
		nf->IOPIN=dev->ID[0];
		nf->cmdstatus=NF_readID_2nd;
		break;
	case NF_readID_2nd:
		nf->IOPIN=dev->ID[1];
		nf->cmdstatus=NF_readID_3rd;
		break;
	case NF_readID_3rd:
		nf->IOPIN=dev->ID[2];
		nf->cmdstatus=NF_readID_4th;
		break;
	case NF_readID_4th:
		nf->IOPIN=dev->ID[3];
		nf->cmdstatus=NF_NOSTATUS;
		nf->iostatus=NF_NONE;
		break;
	default:
	 	NANDFLASH_DBG("Nandflash readID Error!");
		break;
	}
}

static void nandflash_lb_docmd(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
  	//printf("commdlb:%x\n",nf->IOPIN);
	switch(nf->IOPIN) {
  	case NAND_CMD_READ0:
  		nf->cmd=NAND_CMD_READ0;
  		nf->cmdstatus=NF_addr_1st;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
  		nf->pagenum=0;
		nf->iostatus=NF_ADDR;
  		nf->pageoffset=0;
		break;
	case NAND_CMD_READ0b:
		nf->iostatus=NF_DATAREAD;
//		nf->pagenum=0;
//		nf->pageoffset=0;
		break;	
	case NAND_CMD_RESET:
	   	nf->cmd=NAND_CMD_RESET;
		nandflash_lb_reset(dev);
		break;
	case NAND_CMD_SEQIN:
		nf->cmd=NAND_CMD_SEQIN;
  		nf->cmdstatus=NF_addr_1st;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
		memset(nf->writebuffer,0xFF,dev->pagedumpsize);
		nf->iostatus=NF_ADDR;
  		nf->pagenum=0;
		nf->pageoffset=0;
		break;
	case NAND_CMD_RANDIN:
		nf->cmd=NAND_CMD_RANDIN;
  		nf->cmdstatus=NF_addr_1st;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
//		memset(nf->writebuffer,0xFF,dev->pagedumpsize);
		nf->iostatus=NF_ADDR;
		break;
	case NAND_CMD_ERASE1:
		nf->cmd=NAND_CMD_ERASE1;
  		nf->cmdstatus=NF_addr_3rd;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
		nf->iostatus=NF_ADDR;
		nf->pageoffset=0;
  		nf->pagenum=0;
		break;
	case NAND_CMD_ERASE2:
		if ((nf->cmd==NAND_CMD_ERASE1)&&(nf->cmdstatus==NF_addr_finish))
		{
			
			nandflash_lb_doerase(dev,nf);
		}
		else
		{
			NANDFLASH_DBG("invalid ERASE2 commond,command:%x,status:%x\n",nf->cmd,nf->cmdstatus);
		}
		nf->cmd=NAND_CMD_NONE;
		break;
	case NAND_CMD_STATUS:
		nf->cmd=NAND_CMD_STATUS;
  		nf->cmdstatus=NF_status;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
		nf->iostatus=NF_STATUSREAD;
  		break;
  	case NAND_CMD_READID:
  		nf->cmd=NAND_CMD_READID;
  		nf->cmdstatus=NF_addr_5th;
//		printf("%d: cmdstatus=%d\n", __LINE__, nf->cmdstatus);
		nf->pageoffset=0;
		nf->pagenum=0;
		nf->iostatus=NF_ADDR;
  		break;
  	case NAND_CMD_PAGEPROG:
  		if ((nf->cmd==NAND_CMD_SEQIN||nf->cmd==NAND_CMD_RANDIN)&&(nf->cmdstatus==NF_addr_finish))
  		{
			nf->cmd=NAND_CMD_PAGEPROG;
  			nandflash_lb_finishwrite(dev,nf);
  		}
  		else
  		{
  			NANDFLASH_DBG("invalid PAGEPROG commond,command:%x,status:%x\n",nf->cmd,nf->cmdstatus);
  		}
  		break;
	default:
	  	NANDFLASH_DBG("Unknow nandflash command:%x\n",nf->IOPIN);
		exit(0);
		break;
	}
}

static void nandflash_lb_doaddr(struct nandflash_device *dev,struct nandflash_lb_status *nf)
{
	u8 tmp;
	tmp=nf->IOPIN;
//	NANDFLASH_DBG("nandflash_lb_doaddr: cmd=%08hhx, cmdstatus=%d, iopin=%02hhx",
//		nf->cmd, nf->cmdstatus, nf->IOPIN);
	
	switch (nf->cmdstatus) {
	case NF_addr_1st:
		nf->pageoffset = tmp & 0xff;
		nf->cmdstatus=NF_addr_2nd;
		break;
	case NF_addr_2nd:
		nf->pageoffset |= tmp<<8;
		if (nf->cmd==NAND_CMD_RANDIN)
		{
			nf->iostatus=NF_DATAWRITE;
			nf->cmdstatus=NF_addr_finish;
			break;
		}
		nf->cmdstatus=NF_addr_3rd;
		break;
	case NF_addr_3rd:
		nf->pagenum = tmp;
		nf->cmdstatus=NF_addr_4th;
		break;
	case NF_addr_4th:
		nf->pagenum |= tmp << 8;
		nf->cmdstatus=NF_addr_5th;
		break;
	case NF_addr_5th:
		nf->pagenum |= tmp<<16;
		NANDFLASH_DBG("Reading from block %x, page %x, offset %x\n", 
			nf->pagenum / 0x40, nf->pagenum % 40, nf->pageoffset);
		
		if (nf->cmd==NAND_CMD_SEQIN)
		{
			nf->iostatus=NF_DATAWRITE;
			nf->cmdstatus=NF_addr_finish;
			//if (nf->pageoffset!=0) NANDFLASH_DBG("when page program offset is not 0 maybe this is error!\n");
		}
		else if (nf->cmd==NAND_CMD_READID)
		{
			NANDFLASH_DBG("cmd = READID\n");
			nf->iostatus=NF_IDREAD;
			nf->cmdstatus=NF_readID_1st;
		}
		else if (nf->cmd==NAND_CMD_ERASE1)
		{
			nf->iostatus=NF_CMD;
			nf->cmdstatus=NF_addr_finish;
		}
		else
		{
//			NANDFLASH_DBG("Error address input\n");
		}
		break;
	case NF_readID_addr:
		nf->cmdstatus=NF_readID_1st;
		nf->iostatus=NF_addr_finish;
		break;
	case NF_addr_finish:
	 	NANDFLASH_DBG("nandflash write address 4 cycle has already finish,but addr write however!\n");
		break;
	default:
	  	NANDFLASH_DBG("nandflash write address error!\n");
		break;
	}
}

u8   nandflash_lb_readio(struct nandflash_device *dev)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	if (nf->CE==NF_LOW)
	{
		return nf->IOPIN;
	}
	return 0;
}

void nandflash_lb_writeio(struct nandflash_device *dev,u8 iodata)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	if (nf->CE==NF_LOW)
	{
		nf->IOPIN=iodata;
	}
}

void nandflash_lb_setCE(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	nf->CE=state;
	if ((state==NF_HIGH) &(nf->iostatus==NF_DATAREAD))
	{
		nf->iostatus=NF_NONE;
	}
}

void nandflash_lb_setCLE(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	if (nf->ALE==NF_HIGH) 
		NANDFLASH_DBG("warning the ALE is high, but CLE also set high\n");

	nf->CLE=state;
	if ((state==NF_HIGH)&&(nf->CE==NF_LOW))
		nf->iostatus=NF_CMD;
}

void nandflash_lb_setALE(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	if (nf->CLE==NF_HIGH) 
       		NANDFLASH_DBG("warning the CLE is high, but ALE also set high\n");

	nf->ALE=state;
	if ((state==NF_HIGH)&&(nf->CE==NF_LOW))
		nf->iostatus=NF_ADDR;
}

void nandflash_lb_setWE(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;

//	printf("%s we=%d state=%d ce=%d\n", __FUNCTION__, nf->WE, state, nf->CE);

	if ((nf->WE==NF_LOW)&&(state==NF_HIGH)&&(nf->CE==NF_LOW))            //latched on the rising edge
	{
		switch(nf->iostatus) {
		case NF_CMD:
		       nandflash_lb_docmd(dev,nf);
			break;
		case NF_ADDR:
			nandflash_lb_doaddr(dev,nf);
		   	break;
		case NF_DATAWRITE:
			nandflash_lb_dodatawrite(dev,nf);
			//nf->iostatus=NF_NONE;
			break;
		default:
			NANDFLASH_DBG("warning when WE raising,do nothing\n "); 
//			exit(1);
			break;
		}
	}
	nf->WE=state;
}

void nandflash_lb_setRE(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
//	printf("%s re=%d state=%d ce=%d\n", __FUNCTION__, nf->RE, state, nf->CE);
	if ((nf->RE==NF_HIGH)&&(state==NF_LOW)&&(nf->CE==NF_LOW))
	{
		switch(nf->iostatus) {
		case NF_DATAREAD:
//		printf("nf_dataread\n");
			nandflash_lb_doread(dev,nf);
			break;
		case NF_IDREAD:
			nandflash_lb_doreadid(dev,nf);
			break;
		case NF_STATUSREAD:
			nf->IOPIN=nf->status;
			//nf->iostatus=NF_NONE;
			break;
		default:
			if(nf->cmd!=NAND_CMD_READID) 
				NANDFLASH_DBG("warning when RE  falling,do nothing\n "); 
//			exit(1);
			break;
		}
	}
	nf->RE=state;
}

void nandflash_lb_setWP(struct nandflash_device *dev,NFCE_STATE state)
{
	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	nf->WP=state;
	if (state==NF_LOW){
		nf->status=nf->status & 127;
		printf("WP set LOW\n");
	}
	else {
	 	nf->status=nf->status | 128;
		printf("WP set HIGH\n");
	}
}

u32 nandflash_lb_readRB(struct nandflash_device *dev)      //the rb
{
	return 1;
}

void nandflash_lb_sendcmd(struct nandflash_device *dev,u8 cmd)                                         //send a commond
{
//  printf("%s\n", __FUNCTION__);
	nandflash_lb_setCLE(dev,NF_HIGH);
	nandflash_lb_setWE(dev,NF_LOW);
	nandflash_lb_writeio(dev,cmd);
	nandflash_lb_setWE(dev,NF_HIGH);
	nandflash_lb_setCLE(dev,NF_LOW);
}

void nandflash_lb_senddata(struct nandflash_device *dev,u8 data)
{
//  printf("%s\n", __FUNCTION__);
	nandflash_lb_setWE(dev,NF_LOW);
	nandflash_lb_writeio(dev,data);
	nandflash_lb_setWE(dev,NF_HIGH);
}

void nandflash_lb_sendaddr(struct nandflash_device *dev,u8 data)
{
//  printf("%s\n", __FUNCTION__);
	nandflash_lb_setALE(dev,NF_HIGH);
	nandflash_lb_setWE(dev,NF_LOW);
	nandflash_lb_writeio(dev,data);
	nandflash_lb_setWE(dev,NF_HIGH);
	nandflash_lb_setALE(dev,NF_LOW);
}

u8 nandflash_lb_readdata(struct nandflash_device *dev)
{
	u8 data;
	nandflash_lb_setRE(dev,NF_LOW);
	data=nandflash_lb_readio(dev);
	nandflash_lb_setRE(dev,NF_HIGH);
	return data;
}

void nandflash_lb_poweron(struct nandflash_device *dev)
{	
   	struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
	printf("%s\n", __FUNCTION__);
	nf->ALE=NF_LOW;
	nf->CLE=NF_LOW;
	nf->CE=NF_HIGH;
	nf->iostatus=NF_NONE;
	nf->IOPIN=0;
	nf->RB=1;
	nf->RE=NF_HIGH;
	nf->WE=NF_HIGH;
	nf->WP=NF_HIGH;
	nf->status=192;
	nf->pageoffset=0;
	nf->cmd=NAND_CMD_READ0;
	nf->cmdstatus=NF_NOSTATUS;
	nf->iostatus=NF_NONE;
	memset(nf->writebuffer,0xFF,dev->pagedumpsize);         
}

void nandflash_lb_reset(struct nandflash_device *dev)
{
      struct nandflash_lb_status *nf=(struct nandflash_lb_status*)dev->priv;
//	printf("%s\n", __FUNCTION__);
	nf->ALE=NF_LOW;
	nf->CLE=NF_LOW;
	//nf->CE=NF_HIGH;
	nf->iostatus=NF_NONE;
	nf->IOPIN=0;
	nf->RB=1;
	nf->RE=NF_HIGH;
	nf->WE=NF_HIGH;
	nf->WP=NF_HIGH;
	nf->status=192;
	nf->pageoffset=0;
	nf->cmd=NF_NOSTATUS;
	nf->cmdstatus=NF_NOSTATUS;
	nf->iostatus=NF_NONE;
	memset(nf->writebuffer,0xFF,dev->pagedumpsize);
}

void  nandflash_lb_setup(struct nandflash_device* dev)
{
	u8 flag=0xFF;
	int start=0,needinit=0;
	struct stat statbuf;
	struct nandflash_lb_status *nf;
	int i;

	nf = (struct nandflash_lb_status *)malloc(sizeof(struct nandflash_lb_status));
	if (nf==NULL) {
		printf("error malloc nandflash_lb_status!\n");
       		skyeye_exit(-1);
	}

	dev->poweron=nandflash_lb_poweron;
	dev->readdata=nandflash_lb_readdata;
	dev->readio=nandflash_lb_readio;
	dev->readRB=nandflash_lb_readRB;
	dev->reset=nandflash_lb_reset;
	dev->sendaddr=nandflash_lb_sendaddr;
	dev->sendcmd=nandflash_lb_sendcmd;
	dev->senddata=nandflash_lb_senddata;
	dev->setALE=nandflash_lb_setALE;
	dev->setCE=nandflash_lb_setCE;
	dev->setCLE=nandflash_lb_setCLE;
	dev->setRE=nandflash_lb_setRE;
	dev->setWE=nandflash_lb_setWE;
	dev->setWP=nandflash_lb_setWP;
	memset(nf,0,sizeof(struct nandflash_lb_status));
#ifdef POSIX_SHARE_MEMORY_BROKEN
	nf->readbuffer=(u8*)malloc(dev->pagedumpsize);
	if (nf->readbuffer==NULL) {
		printf("error malloc nandflash readbuffer!\n");
       		skyeye_exit(-1);
	}
#endif
	nf->writebuffer=(u8*)malloc(dev->pagedumpsize);
	if (nf->writebuffer==NULL) {
		printf("error malloc nandflash writebuffer!\n");
       		skyeye_exit(-1);
	}
	printf("Flash DumpType = %d\n",dev->dumptype);
	switch(dev->dumptype) {
		case 0:
			nf->pagefmtsize = 0x840;
			break;
		case 1:
			nf->pagefmtsize = 0xa00;
			break;
		default:
			printf("Unknown dumpType!\n");
			nf->pagefmtsize = 0x840;
			break;
	}
	//nf->memsize=528*32*4096;
	if ((nf->fdump= open(dev->dump, FILE_FLAG, 0644)) < 0) {
       		perror("error open nandflash dump: ");
       		skyeye_exit(-1);
	}
	
	if (fstat(nf->fdump, &statbuf) < 0) {  /* need size of input file */
		perror("error fstat function: ");
		skyeye_exit(-1);
	}

	if (statbuf.st_size<dev->devicesize) {
		printf("\nInit nandflash dump file.\n");
		needinit=1;
		start=statbuf.st_size;
		lseek(nf->fdump,dev->devicesize-1,SEEK_SET);
		ssize_t s = write(nf->fdump,&flag,1);
#ifndef __MINGW32__
		fsync(nf->fdump);
#else
		_flushall();
#endif
       }
#ifndef POSIX_SHARE_MEMORY_BROKEN
       
	if (fstat(nf->fdump, &statbuf) < 0) {  /* need size of input file */
		perror("error fstat function: ");
		skyeye_exit(-1);
	}

	printf("k mmap\n");
	printf("file size:%d\n", (int)statbuf.st_size);
	if ((nf->addrspace= mmap(0, statbuf.st_size, PROT_READ | PROT_WRITE,
      		MAP_SHARED, nf->fdump, 0)) == MAP_FAILED) {
		perror("error mmap nandflash file: ");
		skyeye_exit(-1);
      	}

      	if (needinit)
      	{
      		for(i=start;i<dev->devicesize;i++)
      		{
      			*(nf->addrspace+i)=flag;
      		}
      		if (!msync(nf->addrspace,dev->devicesize,MS_SYNC))
			printf("finish init nandflash dump\n");
		
      	}
		
#else
   	nf->curblock=-1;
 	if (needinit)
 	{
 		memset(nf->readbuffer,0xff,dev->pagedumpsize);
 		lseek(nf->fdump,start,SEEK_SET);
 		while((dev->devicesize-start)>=dev->pagesize)
 		{
 		  write(nf->fdump,nf->readbuffer,dev->pagesize);
 		  start=start+dev->pagesize;
 		 }
 		 for (i=start;i<dev->devicesize;i++)
 		   write(nf->fdump,&flag,1);
 	}
#endif
      	dev->priv=nf;
      	nandflash_lb_poweron(dev);
}

void nandflash_lb_uninstall(struct nandflash_device* dev)
{
	struct nandflash_lb_status *nf;
	if (!dev->priv) return;

	nf = (struct nandflash_lb_status*)dev->priv;
	if (nf->fdump) {
#ifndef POSIX_SHARE_MEMORY_BROKEN
		munmap(nf->addrspace,dev->devicesize);
#endif
		close(nf->fdump);
		NANDFLASH_DBG("Uninstall nandflash_lb\n");
	}

#ifdef POSIX_SHARE_MEMORY_BROKEN
	if(nf->readbuffer) free(nf->readbuffer);
#endif

	if(nf->writebuffer) free(nf->writebuffer);
	free(nf);
}
