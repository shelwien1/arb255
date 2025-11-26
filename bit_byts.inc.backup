
struct bit_byts {

  FILE* f;
  int         inuse;
 
  long long   bx;
  long long   ax;
  int         dw;
  int         dr;
  int         d1r;
  int         d1w;
  int         d2w;
  int         d3w;
  int         zerf;
  int         onef;
  int         bn;
  int         bo;
  int         l;
  int         M;
  long        zc;

  void xx() {
    f = NULL;
    inuse = 0x69;
    M = 0x80;
    l = 0;
    bn = 0;
    bo = 0;
    zerf = 0;
    onef = 0;
    zc = 0;

    dw = 1;
    d1w = 0;
    d2w = 0;
    d3w = 0;
    d1r = 0;
    dr = 1;

    bx = 0x7fffffff;
    ax = 16807;
  }

  int status() {
    return inuse;
  }

  void CHK() {
    if( inuse!=0x69 ) { fprintf(stderr, " all read in use bit_byts use error %x \n", inuse); abort(); }
  }

  bit_byts() {
    xx();
  }

  // open file for bit read FOF assumed
  void ir( FILE * fr ) {	
    CHK();
    inuse = 0x01;
    f = fr;
    bn = getc(f);
    if( bn==EOF ) { fprintf(stderr, " empty file in bit_byts \n"); abort(); }
  }

  // open file for 01 read FOF assumed
  void irc( FILE * fr ) {	
    CHK();
    inuse = 0x01;
    f = fr;
    bn = getc(f);
    if( (bn!=(int)'1') && (bn!=(int)'0') ) { fprintf(stderr, " empty file in bit_byts \n"); abort(); }
  }

  // open and read first bit
  int irr( FILE * frr ) {
    ir(frr);
    return r();
  }

  // get next bit psuedo random 
  int rs() {
    if( (d1r=r()) < 0 ) return d1r;
    dr = (ax * dr) % bx;
    return (1 & dr ^ d1r);
  }

  // open file for bit write FOF assumed */
  void iw( FILE* fw ) {	
    CHK();
    inuse = 0x02;
    f = fw;
  }

  // open and write first bit 
  int iww( FILE* fww, int b ) {	
    iw(fww);
    return w(b);
  }

//  int r(); // get next bit
//  int rc(); // get next ASCII 1 or 0
//  int w(int);	// write next bit
//  int wc(int); // write next ASCII 1 or 0

  // on read 0 if normal -1 if last bit -2 there after
  // on write -1 if current is last -2 if call after last one
  // note a big error to issue -2 of no previous 1
  // write bit pusedo random 
  int ws( int c ) {

    if( c==0 ) { d3w++; return 0; }

    if( c==1 ) {
      if( d1w==0 ) { d1w=1; d2w=d3w; d3w=0; return 0; }
      for(; d2w>0; d2w-- ) { dw = (ax * dw) % bx; w(1 & dw); }
      d2w=d3w; d3w=0;
      dw = (ax * dw) % bx;
      return w(1 ^ (1 & dw));
    }

    if( c==-2 ) {
      if( d1w==0 ) return w(-1);
      for(; d2w>0; d2w-- ) { dw = (ax * dw) % bx; w(1 & dw); }
      d1w = 0;
      return w(-1);
    }

    if( d1w==0 ) {
      for(; d3w>0; d3w-- ) { dw = (ax * dw) % bx; w(1 & dw); }
      return w(-1);
    }

    for(; d2w>0; d2w-- ) { dw = (ax * dw) % bx; w(1 & dw); }
    dw = (ax * dw) % bx;
    w(1 ^ (1 & dw));

    for(; d3w>0; d3w-- ) { dw = (ax * dw) % bx; w(1 & dw); }
    d1w = 0;

    return w(-1);
  }

  // on read 0 if normal -1 if last bit -2 there after
  // on write -1 if current is last -2 if call after last one
  int wz(int c) {
    if( c==-2 ) return w(-2);
    if( c==0 ) { bn++; return 0;
    } else {
      for(; bn>0; bn-- ) w(0);
      return w(c);
    }
  }

  int wzc(int c) {
    if( c==-2 ) return wc(-2);
    if( c==0 ) { bn++; return 0;
    } else {
      for(; bn>0; bn-- ) wc(0);
      return wc(c);
    }
  }

  // FILE of ASCII ones and zeroes only
  int rc() {				
    if( f==NULL ) return -2;

    if( bn==2 ) {
      bo = fgetc(f);
      if( bo==(int)'1' ) return 1;
      if( bo==(int)'0' ) return 0;
      xx();
      return -1;
    }

    if( bn==(int)'1' ) {
      bo = fgetc(f);
      if( bo==(int)'1' ) return 1; // one in a string of all ones
      if( bo==(int)'0' ) { bn = 1; return 1; }
      xx();
      return -1;
    }

    if( bn==(int)'0' ) { bn = 2; return 0; }

    if( bn==1 ) { bn = 2; return 0; }

    return 7;
  }

  int r() {
    // return -1 on last bit which is last one in file -2 thereafter
    // return 0 on zero return 1 most of the time one
    if( f==NULL ) return -2;

    if( (l>>=1)==0 ) {
      l = 0x80;
      bo = bn;
      bn = fgetc(f);
      if( bo==0 ) zerf = 1;
      else if( bo!=M ) { zerf = 0; onef = 0; } 
      else if( zerf==1 ) onef = 1;
      if( (bn==EOF) && (onef+zerf)>0 ) { onef = 0; zerf = 0; bn = M; }
    }

    if( (bo & l)==0 ) return 0;

    bo ^= l;
    if( (bn!=EOF) || (bo!=0) ) return 1;

    xx();
    return -1;
  }

  // like w() but writes ascii one or zero  returns 0 if ok
  // return -1 if sending last byte
  // return -2 if closed longer;
  // 0 or 1 writes and -1 writes last one -2 means previous 1 was last
  int wc(int x) {
    if( f==NULL ) return -2;

    if( x==1 ) { fputc('1',f); return 0; }

    if( x==0 ) { fputc('0',f); bo = 1; }

    if( x==-2 ) bo = 1;

    if( x==-1 ) { 
      if( bo==0 ) fputc('1', f);
      xx();
      return x;
    }
    return 0;
  }

  // return 0 if ok
  // return -1 if sending last byte
  // return -2 if closed longer;
  // 0 or 1 writes and -1 writes last one -2 means previous 1 was last
  int w( int x ) {

    if( f==NULL ) return -2;

    if( x==-1 ) { w(1); w(-2); return -1; }

    if( x==-2 ) {
      if( (bo==M) && ((zerf+onef)==0) ) fputc(bo, f);
      if( (bo==M) || (bo==0) ) { xx(); return -2; }
    }

    if( (l>>=1)==0 ) l = 0x80;

    if( x>0 ) bo ^= l;

    if( (l==1) || (x<0) ) {
      if( bo==0 ) zerf = 1;
      else if( bo!=M ) { zerf = 0; onef = 0; } 
      else if( zerf==1 ) onef = 1;
      if( x<0 ) {
	if( ((onef+zerf)==0) || (bo!=M) ) fputc(bo, f);
  	xx();
  	return -2;
      } else {
  	fputc(bo, f);
  	bo = 0;
      }
    }
    return 0;
  }

};
