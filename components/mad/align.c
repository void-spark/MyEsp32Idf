unsigned char unalChar(const unsigned char *adr) {
	int *p=(int *)((int)adr&0xfffffffc);
	int v=*p;
	int w=((int)adr&3);
	if (w==0)  {
		return ((v>>0)&0xff);
	} else if (w==1) {
		return ((v>>8)&0xff);
	} else  if (w==2) {
		return ((v>>16)&0xff);
	} else if (w==3) {
		return ((v>>24)&0xff);
	} else {
		return -1;
	}
}


unsigned short unalShort(const unsigned short *adr) {
	int *p=(int *)((int)adr&0xfffffffc);
	int v=*p;
	int w=((int)adr&3);
	if (w==0) return (v&0xffff); else return (v>>16);
}